#!/usr/bin/env python3
#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

'''
This script is used to extract the Bluetooth Mesh composition data from the build folder of
an application. This information is used to generate a hash that can verify if the composition
of a device will change after a Mesh device firmware upgrade has completed.
The input to this script is the following content of the build folder of an application:
    - build/zephyr/zephyr.elf
    - build/zephyr/.config
The script produces the following output to the build folder:
	- build/zephyr/dfu_application.zip_mesh_metadata.json
This output is also appended to the archive located at build/zephyr/dfu_application.zip.
'''

import struct
import sys
import os
from elftools.elf.elffile import ELFFile
import json
from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.ciphers import algorithms
from zipfile import ZipFile
import traceback
import argparse

FILE_NAME_IN_ZIP = 'ble_mesh_metadata.json'
FILE_NAME = 'dfu_application.zip_ble_mesh_metadata.json'


def exit_with_error_msg():
    traceback.print_exc()
    print("Extracting BLE Mesh metadata failed")
    print("You can bypass this script by disabling the CONFIG_BT_MESH_DFU_METADATA_ON_BUILD option in your project config")
    sys.exit(0)


class Model:
    @staticmethod
    def create_sig_model(elem_idx: int, mod_idx: int, address: int, id: int):
        return Model(elem_idx, mod_idx, address, id)

    @staticmethod
    def create_vnd_model(self, elem_idx: int, mod_idx: int, address: int, id: int, cid: int):
        return Model(elem_idx, mod_idx, addr, id, True, cid)

    def __init__(self, elem_idx: int, mod_idx: int, address: int, id: int, vnd: bool = False, cid: int = 0):
        self.elem_idx = elem_idx
        self.mod_idx = mod_idx
        self.address = address
        self.id = id
        self.vnd = vnd
        self.cid = cid
        self.extends = []
        self.corresponds = []
        self.cor_group_id = -1 # -1 means no group id

    def model_id(self) -> int:
        if self.vnd:
            return self.cid << 16 | self.id
        return self.id

    def extends_add(self, address: int):
        self.extends.append(address)

    def corresponds_add(self, address: int):
        self.corresponds.append(address)


class Elem:
    def __init__(self, loc, idx: int):
        self.loc = loc
        self.vnd_list = []
        self.sig_list = []
        self.idx = idx
        self.sig_idx = 0
        self.vnd_idx = 0

    def vnd_model_add(self, address, cid, vid):
        model = Model.create_vnd_model(self.idx, self.vnd_idx, address, cid, True, vid)
        self.vnd_list.append(model)
        self.vnd_idx += 1
        return model

    def sig_model_add(self, address, id):
        model = Model.create_sig_model(self.idx, self.sig_idx, address, id)
        self.sig_list.append(model)
        self.sig_idx += 1
        return model

    def bytestring_generate(self):
        bytestring = bytearray()
        bytestring.extend(self.loc.to_bytes(2, 'little'))

        sig_list = [model.model_id() for model in self.sig_list]
        vnd_list = [model.model_id() for model in self.vnd_list]
        bytestring.append(len(sig_list))
        bytestring.append(len(vnd_list))

        for sig in self.sig_list:
            bytestring.extend(sig.model_id().to_bytes(2, 'little'))
        for vnd in self.vnd_list:
            bytestring.extend(vnd.model_id().to_bytes(4, 'little'))

        return bytestring

    def dict_generate(self):
        return {
            "location": self.loc,
            "sig_models": [ model.model_id() for model in self.sig_list ],
            "vendor_models": [ model.model_id() for model in self.vnd_list ],
        }


class Comp0:
    # Must stay in order
    FEATURE_KCONF_OPTS = [
        'CONFIG_BT_MESH_RELAY',
        'CONFIG_BT_MESH_GATT_PROXY',
        'CONFIG_BT_MESH_FRIEND',
        'CONFIG_BT_MESH_LOW_POWER',
    ]

    def __init__(self, cid, pid, vid, kconfig):
        if 'CONFIG_BT_MESH_CRPL' not in kconfig.keys():
            raise Exception("Could not find CONFIG_BT_MESH_CRPL Kconfig option")
        self.elems = []
        self.cid = cid
        self.pid = pid
        self.vid = vid
        self.__features_add(kconfig)
        self.hash = None
        self.elem_idx = 0

    def elem_add(self, loc):
        new_elem = Elem(loc, self.elem_idx)
        self.elem_idx += 1
        self.elems.append(new_elem)
        return new_elem

    def __features_add(self, kconfig):

        self.feat = 0
        self.crpl = int(kconfig['CONFIG_BT_MESH_CRPL'])

        for i, opt in enumerate(self.FEATURE_KCONF_OPTS):
            self.feat += (1 if kconfig.get(opt) == 'y' else 0) << i

    def __bytestring_generate(self):
        bytestring = bytearray()
        bytestring.extend(self.cid.to_bytes(2, 'little'))
        bytestring.extend(self.pid.to_bytes(2, 'little'))
        bytestring.extend(self.vid.to_bytes(2, 'little'))
        bytestring.extend(self.crpl.to_bytes(2, 'little'))
        bytestring.extend(self.feat.to_bytes(2, 'little'))

        for elem in self.elems:
            bytestring.extend(elem.bytestring_generate())

        return bytestring

    def dict_generate(self):
        return {
            "cid": self.cid,
            "pid": self.pid,
            "vid": self.vid,
            "crpl": self.crpl,
            "features": self.feat,
            "elements": [e.dict_generate() for e in self.elems]
        }

    def hash_generate(self):
        # Uses 16-byte zero key
        crypto = cmac.CMAC(algorithms.AES(bytes(16)))
        crypto.update(bytes(self.__bytestring_generate()))
        self.hash, *_ = struct.unpack('<L', crypto.finalize()[:4])
        return self.hash

    def find_model_by_address(self, address):
        for elem in self.elems:
            for model in elem.sig_list + elem.vnd_list:
                if model.address == address:
                    return model
        return None

    def is_extended_model_items_format_long(self, model: Model) -> bool:
        """
        Check format for Extended_Model_Items indicator.

        Parameters:
            model (Model): Model object
        Returns:
            bool: True if the format is long, False if the format is short
        """
        for extended_model_addr in model.extends:
            extended_model = self.find_model_by_address(extended_model_addr)

            if extended_model is None:
                raise Exception(f"Extended model not found: {extended_model_addr:02x}")

            elem_offset = model.elem_idx - extended_model.elem_idx

            if (elem_offset > 3) or (elem_offset < -4) or (extended_model.mod_idx > 31):
                return True

        return False

    def has_corresponding_group_id(self, model: Model) -> bool:
        """
        Check if the model has a corresponding group ID.

        Parameters:
            model (Model): Model object
        Returns:
            bool: True if the model has a corresponding group ID, False otherwise
        """
        return model.cor_group_id >= 0

    def prepare_model_item_header(self, model: Model, format_long: bool) -> bytearray:
        bytestring = bytearray()

        cor_present = self.has_corresponding_group_id(model)
        model_elem_info = 0

        if cor_present:
            model_elem_info |= (1 << 0)

        if format_long:
            model_elem_info |= (1 << 1)

        model_elem_info |= (len(model.extends) << 2)
        bytestring.extend(model_elem_info.to_bytes(1, 'little'))
        print(f'Encoded model_elem_info: {model_elem_info:02x}, cor_present: {cor_present}, format_long: {format_long}, extends count: {len(model.extends):02x}')

        if cor_present:
            bytestring.extend(model.cor_group_id.to_bytes(1, 'little'))

        return bytestring

    def add_items_to_page(self, model: Model, format_long: bool):
        bytestring = bytearray()

        for extended_model_address in model.extends:
            extended_model = self.find_model_by_address(extended_model_address)
            if extended_model is None:
                raise Exception(f"Extended model not found: {extended_model_address:02x}")

            elem_offset = model.elem_idx - extended_model.elem_idx

            if format_long is False:
                if elem_offset < 0:
                    elem_offset += 8

                extended_model_item = (elem_offset) | (extended_model.mod_idx << 3)
                print(f'Packing short: {elem_offset:02x}, {extended_model.mod_idx:02x}, {extended_model_item:02x})')
                bytestring.extend(extended_model_item.to_bytes(1, 'little'))
            else:
                if elem_offset < 0:
                    elem_offset += 256

                bytestring.extend(elem_offset.to_bytes(1, 'little'))
                bytestring.extend(extended_model.mod_idx.to_bytes(1, 'little'))

        return bytestring

    def corresponding_group_id_generate(self):
        """
        Generate Corresponding Group ID for each model.

        """
        cor_group_id = 0

        for elem in self.elems:
            for model in elem.sig_list + elem.vnd_list:
                if len(model.corresponds) == 0:
                    continue

                for corresponding_model_addr in model.corresponds:
                    print(f'Model [{model.elem_idx}:{model.mod_idx}], Corresponding Model Address: {corresponding_model_addr:02x}')
                    corresponding_model = self.find_model_by_address(corresponding_model_addr)
                    print(f"Model [{model.elem_idx}:{model.mod_idx}], Corresponding Model: [{corresponding_model.elem_idx}:{corresponding_model.mod_idx}]")

                    if self.has_corresponding_group_id(corresponding_model):
                        model.cor_group_id = corresponding_model.cor_group_id

                        print(f'Model got corresponding group ID: {model.cor_group_id:02x}')
                    elif self.has_corresponding_group_id(model):
                        corresponding_model.cor_group_id = model.cor_group_id

                        print(f'Corresponding model got corresponding group ID: {corresponding_model.cor_group_id:02x}')
                    else:
                        print(f"Both models got new corresponding group ID: {cor_group_id:02x}")

                        model.cor_group_id = cor_group_id
                        corresponding_model.cor_group_id = cor_group_id
                        cor_group_id += 1

    def page_1_generate(self):
        """
        Generate Composition data page 1 of the composition data.

        Parameters:
            comp (Comp0): Composition data object
        Returns:
            Comp1: Bytestring with encoded Composition data page 1
        """

        # Fill up Corresponding_Group_IDs
        self.corresponding_group_id_generate()

        page_1_bs = bytearray()

        for elem in self.elems:
            print(f"Element {elem.idx}: Number of SIG models: {len(elem.sig_list)}, Number of VND models: {len(elem.vnd_list)}")
            # See Table 4.7, MshPRTv1.1
            page_1_bs.extend(len(elem.sig_list).to_bytes(1, 'little'))
            page_1_bs.extend(len(elem.vnd_list).to_bytes(1, 'little'))

            for model in elem.sig_list + elem.vnd_list:
                format_long = self.is_extended_model_items_format_long(model)
                page_1_bs.extend(self.prepare_model_item_header(model, format_long))

                print(f'Model [{model.elem_idx}:{model.mod_idx}]: Corresponding Group ID: {model.cor_group_id}, format long: {format_long}, extends count: {len(model.extends):02x}')

                if len(model.extends) > 0:
                    page_1_bs.extend(self.add_items_to_page(model, format_long))

        return page_1_bs


class KConfig(dict):

    @classmethod
    def from_file(cls, filename):
        """
        Extracts content of '.config' file into a dictionary

        Parameters:
            config_path (str): Path to '.config' file used by a firmware
        Returns:
            cls: A KConfig object where keys are Kconfig option names and values
            are option values, parsed from the config file at config_path.
        """
        configs = cls()
        try:
            with open(filename, 'r') as config:
                for line in config:
                    if not line.startswith("CONFIG_"):
                        continue
                    kconfig, value = line.split("=", 1)
                    configs[kconfig] = value.strip()
                return configs
        except Exception as err :
            raise Exception("Unable to parse .config file") from err

    def version_parse(self):
        try:
            clean_str = self['CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION'].replace("+", ".").replace("\"", "")
            version_list = [int(s) for s in clean_str.split(".") if s.isdigit()]
            return {
                "major": version_list[0],
                "minor": version_list[1],
                "revision": version_list[2],
                "build_number": version_list[3],
            }
        except Exception as err :
            print(f"Unable to parse CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION Kconfig option, using 0.0.0.0")
            return {
                "major": 0,
                "minor": 0,
                "revision": 0,
                "build_number": 0,
            }
#            raise Exception("Unable to parse CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION Kconfig option") from err


def read_data_by_address(elf, address, size):
    """
    Reads value from the .elf file at the specified address.
    """
    file_offset = None
    for segment in elf.iter_segments():
        if segment.header['p_type'] != 'PT_LOAD':
            continue
        if (address >= segment['p_vaddr']) and\
            (address < segment['p_vaddr'] + segment['p_filesz']):
            file_offset = address - segment['p_vaddr'] + segment['p_offset']
            break
    else:
        raise Exception('Error getting file offset from ELF data')
    elf.stream.seek(file_offset)

    return elf.stream.read(size)


def read_symbol_data(elf, symbol_addr):
    """
    Reads variable data from the '.symtab' section of the .elf file.

    Parameters:
        elf (ELFFile): ELFFile instance
        symbol_addr (int): Address of the variable to read

    Returns:
        bytearray: Data read from the specified address
    """
    section = elf.get_section_by_name('.symtab')
    if section is None:
        raise Exception('Unable to find .symtab section')
    symbol = None
    closest_symbol = None
    for s in section.iter_symbols():
        if (s.entry.st_value == symbol_addr) and\
            (len(s.name) > 0) and\
            ("$" not in s.name) and\
            s.entry.st_size > 0:
                symbol = s
                break
    else:
        raise Exception(f'Unable to find symbol at address {symbol_addr:02x}')

    file_offset = None

    for segment in elf.iter_segments():
        if segment.header['p_type'] != 'PT_LOAD':
            continue
        if (symbol['st_value'] >= segment['p_vaddr']) and\
            (symbol['st_value'] < segment['p_vaddr'] + segment['p_filesz']):
            file_offset = symbol['st_value'] - segment['p_vaddr'] + segment['p_offset']
            break
    else:
        raise Exception('Error getting file offset from ELF data')
    elf.stream.seek(file_offset)
    sz = symbol['st_size']

    return elf.stream.read(sz)

def find_comp_data_from_dwarf(elf_path):
    """
    Find all occurrences of the `bt_mesh_comp` variable in the .elf file

    The composition data declaration must have the const-qualifier. It can also be declared as an array. Example:
        ```
        static const struct bt_mesh_comp comp;
        const struct bt_mesh_comp comp;
        const struct bt_mesh_comp comp[2];
        ```
    Parameters:
        elf_path (ELFFile): ELFFile instance

    Returns:
        List(addr): Addresses of the found composition data instances in the firmware.
    """
    DW_OP_addr = 0x3

    with open(elf_path, 'rb') as file:
        elf_file = ELFFile(file)
        dwarf_info = elf_file.get_dwarf_info()
        comp_data_arr = []
        for cu_header in dwarf_info.iter_CUs():
            for die in cu_header.iter_DIEs():
                if die.tag != 'DW_TAG_variable':
                    continue
                location = die.attributes.get('DW_AT_location')
                if location is None:
                    continue
                if location.form not in ("DW_FORM_exprloc"):
                    continue
                opcode = location.value[0]
                if opcode != DW_OP_addr:
                    continue

                addr = int.from_bytes(die.attributes.get('DW_AT_location').value[1:5], 'little')

                if 'DW_AT_abstract_origin' in die.attributes and \
                    die.attributes.get('DW_AT_abstract_origin').form == 'DW_FORM_ref_addr':
                    # If address is moved to another die, find original variable through the
                    # reference and continue with the new die.
                    value = die.attributes.get('DW_AT_abstract_origin').value
                    die = dwarf_info.get_DIE_from_refaddr(value)
                    if die is None:
                        continue

                # Check that the variable type is either `const struct bt_mesh_comp` or
                # `const struct bt_mesh_comp[]`.
                exp_tags = [
                    ['DW_TAG_const_type', 'DW_TAG_structure_type'],
                    ['DW_TAG_const_type', 'DW_TAG_array_type', 'DW_TAG_const_type', 'DW_TAG_structure_type'],
                ]
                type_die = die
                max_length = max(len(arr) for arr in exp_tags)
                try:
                    for i in range(max_length):
                        type_die_type = type_die.attributes.get('DW_AT_type')
                        if type_die_type is None:
                            raise Exception('DW_AT_type is missing')
                        type_die = type_die.get_DIE_from_attribute('DW_AT_type')
                        for tags in exp_tags:
                            if len(tags) > i and type_die.tag == tags[i]:
                                break
                        else:
                            raise Exception('Wrong DW_AT_type')
                        name = type_die.attributes.get('DW_AT_name')
                        if name and name.value == b'bt_mesh_comp':
                            break
                    else:
                        continue
                except Exception:
                    continue

                comp_data_arr.append(addr)

        if comp_data_arr is None or len(comp_data_arr) == 0:
            raise Exception("Could not find composition data in .elf file")

        return comp_data_arr


def read_comp_data(elf_path, addr, kconfigs):
    """
    Reads content of the composition data variable from .elf file.

    Parameters:
        elf_path (ELFFile): ELFFile instance
        addr (int): Address of the composition data variable
        kconfigs (KConfig): A KConfig object representing Kconfig options used for the firmware to compile with
    Returns:
        Tuple:
        - First element: parsed Composition data page 0
        - Second element: parsed Composition data page 1
    """

    label_cnt = int(kconfigs['CONFIG_BT_MESH_LABEL_COUNT']) if 'CONFIG_BT_MESH_LABEL_COUNT' in kconfigs.keys() else 0
    lcd_srv = (kconfigs['CONFIG_BT_MESH_LARGE_COMP_DATA_SRV'] == 'y') if 'CONFIG_BT_MESH_LARGE_COMP_DATA_SRV' in kconfigs.keys() else False

    with open(elf_path, 'rb') as elf_file:
        elf = ELFFile(elf_file)
        cdp0_value = read_symbol_data(elf, addr)

        # The format of the composition data is defined by `struct bt_mesh_comp` type.
        # The format of an element is defined by `struct bt_mesh_elem` type.
        # The format of a model is defined by `struct bt_mesh_model` type.
        # All types are declared in `zephyr/include/zephyr/bluetooth/mesh/access.h`.

        # Legend:
        # H - uint16_t, I - uint32_t, B - uint8_t

        # H - cid
        # H - pid
        # H - vid
        # H - crpl
        # H - feat
        # I - elem_count
        # I - elem_ptr

        for comp_data_entry in struct.iter_unpack('HHHHII', cdp0_value):
            cid, pid, vid, _, elems_count, elems_ptr = comp_data_entry

            comp = Comp0(cid, pid, vid, kconfigs)

            # Legend:
            # H - uint16_t, I - uint32_t, B - uint8_t

            # I - rt
            # H - loc
            # B - sig_count
            # B - vnd_count
            # I - models (SIG models)
            # I - vnd_models (Vendor models)

            elems_value = read_symbol_data(elf, elems_ptr)
            elems_iter = struct.iter_unpack('IHBBII', elems_value)
            i = 0

            for elem in elems_iter:
                i += 1
                if i > elems_count:
                    raise Exception('Extracted more elems than \'elem_count\'')
                __rt, loc, sig_count, vnd_count, sig_ptr, vnd_ptr = elem
                elem_item = comp.elem_add(loc)

                def models_unpack(ptr, elem_item, vnd):
                    # models_array is an array of pointers to models which can be stored either in
                    # RAM or in flash.
                    models_array = read_symbol_data(elf, ptr)

                    models_iter = struct.iter_unpack('I', models_array)

                    for model_addr, in models_iter:
                        print(f"Model address: {model_addr:02x}")
                        model_format = 'HHIIHHIHHIIHHIHH' + ('I' if label_cnt > 0  else '') + 'II' + ('I' if lcd_srv else '')

                        # Read content (`struct bt_mesh_model`) of the model
                        model_value = read_data_by_address(elf, model_addr, struct.calcsize(model_format))

                        # Legend:
                        # H - uint16_t, I - uint32_t, B - uint8_t

                        # H - vnd.company_id (or SIG model id),
                        # H - vnd.id,
                        # I - rt

                        ################################
                        # I - extends
                        # H - extends_cnt
                        # H - GAP (alignment)
                        # I - corresponds
                        # H - corresponds_cnt
                        # H - GAP (alignment)
                        ################################

                        # I - pub
                        # I - keys
                        # H - keys_cnt
                        # H - GAP (alignment)
                        # I - groups
                        # H - groups_cnt
                        # H - GAP (alignment)
                        # I - uuids
                        # I - op
                        # I - cb
                        # I - metadata

                        model_value_unpacked = struct.iter_unpack(model_format, model_value)

#                        print(f'Models value: {len(list(model_value_unpacked))}')

                        model_value_unpacked = list(model_value_unpacked)[0]

                        id1, id2, __rt, extends, extends_cnt, _, corresponds, corresponds_cnt, _, __pub, __keys, __keys_cnt, _, __groups, __groups_cnt, _, __uuids, __op, __cb = model_value_unpacked
                        print(f"Model: {id1:04x}, {id2:04x}")

                        model = None
                        if not vnd:
                            model = elem_item.sig_model_add(model_addr, id1)
                        else:
                            model = elem_item.vnd_model_add(model_addr, id1, id2)

                        print(f"Extends: {extends:08x}, {extends_cnt:04x}")
                        if (extends != 0) and extends_cnt > 0:
                            extends_array = read_data_by_address(elf, extends, extends_cnt * 4)
                            extends_iter = struct.iter_unpack('I', extends_array)
                            for extended_model_addr, in extends_iter:
                                model.extends_add(extended_model_addr)
                                print(f"Extends: {extended_model_addr:08x}")

                        print(f"Corresponds: {corresponds:08x}, {corresponds_cnt:04x}")
                        if (corresponds != 0) and corresponds_cnt > 0:
                            corresponds_array = read_data_by_address(elf, corresponds, corresponds_cnt * 4)
                            corresponds_iter = struct.iter_unpack('I', corresponds_array)
                            for corresponding_model_addr, in corresponds_iter:
                                print(f"Corresponds: {corresponding_model_addr:08x}")
                                model.corresponds_add(corresponding_model_addr)

                if sig_count > 0:
                    models_unpack(sig_ptr, elem_item, False)
                if vnd_count > 0:
                    models_unpack(vnd_ptr, elem_item, True)
            yield comp

def parse_comp_data(elf_path, kconfigs):
    """
    Extract composition data from .elf and .config file.

    Parameters:
        kconfigs (KConfig): A KCoonfig object representing Kconfig options used for the firmware to compile with
    Returns:
        List of parsed composition data
    """
    try:
        addrs = find_comp_data_from_dwarf(elf_path)
        return [comp
                for addr in addrs
                for comp in read_comp_data(elf_path, addr, kconfigs)]
    except Exception as err:
        raise Exception("Failed to extract composition data from .elf file") from err

def encoded_metadata_get(version, comp, binary_size, core_type):
    elem_cnt = len(comp.elems)

    bytestring = bytearray()
    bytestring.append(version["major"])
    bytestring.append(version["minor"])
    bytestring.extend(version["revision"].to_bytes(2, 'little'))
    bytestring.extend(version["build_number"].to_bytes(4, 'little'))
    bytestring.extend(binary_size.to_bytes(3, 'little'))
    bytestring.append(core_type)
    bytestring.extend(comp.hash_generate().to_bytes(4, 'little'))
    bytestring.extend(elem_cnt.to_bytes(2, 'little'))
    return bytestring

def input_parse():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('--bin-path', required=True, type=str)
    parser.add_argument('--print-metadata', action='store_true')
    return parser.parse_known_args()[0]

def existing_metadata_print(path):
    try:
        metadata_file = open(path, 'r')
        print(json.dumps(json.load(metadata_file), indent=4))
    except Exception as err :
        raise Exception("Failed to get existing metadata") from err

if __name__ == "__main__":
    try:
        args = input_parse()

        sysbuild_config_path = os.path.abspath(os.path.join(args.bin_path, '.config.sysbuild'))

        if os.path.isfile(sysbuild_config_path):
            # Sysbuild
            zip_path = os.path.abspath(os.path.join(args.bin_path, '..', '..', 'dfu_application.zip'))
            sysbuild = True
        else:
            # Child/parent image
            zip_path = os.path.abspath(os.path.join(args.bin_path, 'dfu_application.zip'))
            sysbuild = False

        metadata_path = os.path.abspath(os.path.join(args.bin_path, FILE_NAME))
        config_path = os.path.abspath(os.path.join(args.bin_path, '.config'))
        kconfigs = KConfig.from_file(config_path)
        kernel_name = kconfigs['CONFIG_KERNEL_BIN_NAME'].replace("\"", "")
        elf_path = os.path.abspath(os.path.join(args.bin_path, (kernel_name + '.elf')))

        if args.print_metadata:
            # Caller requests already generated metadata
            existing_metadata_print(metadata_path)
            sys.exit(0)

#        zip = ZipFile(zip_path, "a")
#        if FILE_NAME_IN_ZIP in zip.namelist():
            # Mesh metadata already present in zip file
#            sys.exit(0)

        comps = parse_comp_data(elf_path, kconfigs)

        version = kconfigs.version_parse()

        binary_size = 0
#        if sysbuild:
#            binary_size = os.path.getsize(os.path.join(args.bin_path, (kernel_name + '.signed.bin')))
#        else:
#            binary_size = os.path.getsize(os.path.join(args.bin_path, 'app_update.bin'))
        core_type = 1
        json_data = []

        for comp in comps:
            page_1 = comp.page_1_generate()
            encoded_metadata = encoded_metadata_get(version, comp, binary_size, core_type)
            json_data.append({
                "sign_version": version,
                "binary_size": binary_size,
                "core_type": core_type,
                "composition_data": comp.dict_generate(),
                "composition_hash": str(hex(comp.hash_generate())),
                "encoded_metadata": str(encoded_metadata.hex()),
                "page_1": str(page_1.hex()),
            })

        print(json.dumps(json_data, indent=4))

#        with open(metadata_path, "w") as outfile:
#            outfile.write(json.dumps(json_data if len(json_data) > 1 else json_data[0], indent=4))
#        zip.write(metadata_path, FILE_NAME_IN_ZIP)
#        zip.close()

        print("Bluetooth Mesh Composition metadata generated:")
        if len(json_data) > 1:
            print(f"\t{len(json_data)} composition data instances found.")
            print(f"\tAll metadata variants written to: {zip_path}")
        else:
            print(f"\tEncoded metadata: {json_data[0]['encoded_metadata']}")
            print(f"\tFull metadata written to: {zip_path}")
    except Exception:
        exit_with_error_msg()
