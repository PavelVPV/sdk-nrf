import struct

class ModRelation:
    def __init__(self, elem_base, idx_base, elem_ext, idx_ext, relation_type):
        self.elem_base = elem_base
        self.idx_base = idx_base
        self.elem_ext = elem_ext
        self.idx_ext = idx_ext
        self.type = relation_type # TODO: Rename to cor_id

class BtMeshComp:
    def __init__(self):
        self.mod_rel_list = []

    def add_relation(self, elem_base, idx_base, elem_ext, idx_ext, relation_type):
        self.mod_rel_list.append(ModRelation(elem_base, idx_base, elem_ext, idx_ext, relation_type))

    def bt_mesh_model_correspond(self, corresponding_mod, base_mod):
        cor_id = 0
        for rel in self.mod_rel_list:
            if (self.is_model_base(base_mod, rel) or self.is_model_extension(base_mod, rel) or
                self.is_model_base(corresponding_mod, rel) or self.is_model_extension(corresponding_mod, rel)) and rel.type < 0xFF:
                cor_id = rel.type
                break
        else:
            cor_id = max((rel.type for rel in self.mod_rel_list if rel.type < 0xFF), default=0) + 1
        self.add_relation(base_mod['elem_idx'], base_mod['mod_idx'] + base_mod.get('vnd_offset', 0),
                     corresponding_mod['elem_idx'], corresponding_mod['mod_idx'] + corresponding_mod.get('vnd_offset', 0),
                     cor_id)

    def bt_mesh_model_extend(self, extending_mod, base_mod):
        self.add_relation(base_mod['elem_idx'], base_mod['mod_idx'] + base_mod.get('vnd_offset', 0),
                     extending_mod['elem_idx'], extending_mod['mod_idx'] + extending_mod.get('vnd_offset', 0),
                     0xFF)

    def is_model_base(self, model, rel):
        return model['elem_idx'] == rel.elem_base and (model['mod_idx'] + model.get('vnd_offset', 0)) == rel.idx_base

    def is_model_extension(self, model, rel):
        return model['elem_idx'] == rel.elem_ext and (model['mod_idx'] + model.get('vnd_offset', 0)) == rel.idx_ext

    def count_mod_ext(self, model):
        offset_record = 0
        extensions = 0

        for rel in self.mod_rel_list:
            if self.is_model_extension(model, rel) and rel.type == 0xFF:
                extensions += 1

                offset = rel.elem_ext - rel.elem_base
                if (abs(offset) > abs(offset_record)):
                    offset_record = offset

        return offset_record, extensions

    def is_cor_present(self, model):
        for rel in self.mod_rel_list:
            if (self.is_model_base(model, rel) or self.is_model_extension(model, rel)) and rel.type < 0xFF:
                return True, rel.type

        return False, None

    def prep_model_item_header(self, model, buf):
        offset_record, ext_mod_cnt = self.count_mod_ext(model)
        cor_present, cor_id = self.is_cor_present(model)

        mod_elem_info = ext_mod_cnt << 2
        if ext_mod_cnt > 31 or offset_record < -4 or offset_record > 3:
            mod_elem_info |= 0b10

        if cor_present:
            mod_elem_info |= 0b01

        self.data_buf_add_u8_offset(buf, mod_elem_info)

        if cor_present:
            self.data_buf_add_u8_offset(buf, cor_id)

        return ext_mod_cnt

    def data_buf_add_u8_offset(self, buf, val):
        # TODO: offset is not supported yet
        buf.append(val)

    def add_items_to_page(self, buf, model, ext_mod_cnt):
        for rel in self.mod_rel_list:
            if not (self.is_model_extension(model, rel) and rel.type == 0xFF):
                continue

            elem_offset = model['elem_idx'] - rel.elem_base
            mod_idx = rel.idx_base

            if ext_mod_cnt < 32 and elem_offset < 4 and elem_offset > -5:
                # short format
                if elem_offset < 0:
                    elem_offset += 8

                elem_offset |= mod_idx << 3
                self.data_buf_add_u8_offset(buf, elem_offset)
            else:
                # long format
                if elem_offset < 0:
                    elem_offset += 256

                self.data_buf_add_u8_offset(buf, elem_offset)
                self.data_buf_add_u8_offset(buf, mod_idx)

    def bt_mesh_comp_data_get_page_1(self, comp):
        # TODO: offset is not supported yet

        binary_output = bytearray()

        for elem_idx, elem in enumerate(comp):
            buf = bytearray()

            sig = elem.get('sig', [])
            vnd = elem.get('vnd', [])

            buf = struct.pack(
                "<BB",  # Little-endian, 4 unsigned bytes and 1 unsigned byte
                len(sig),
                len(vnd),
            )

            binary_output.extend(buf)

            buf = bytearray()

            for (models, offset) in [(sig, 0), (vnd, len(sig))]:
                for model_idx, model in enumerate(models):
                    model['elem_idx'] = elem_idx
                    model['mod_idx'] = model_idx if offset == 0 else model_idx + offset
                    model["vnd_offset"] = offset

                    ext_mod_cnt = self.prep_model_item_header(model, buf)

                    if ext_mod_cnt != 0:
                        self.add_items_to_page(buf, model, ext_mod_cnt)

            binary_output.extend(buf)

        return bytes(binary_output)

# Example usage
bt_mesh = BtMeshComp()

# Sample composition array
comp = [
    {
        "sig": [
            {"ext": [{"elem_idx": 1, "mod_idx": 0}], "cor": []},
            {"ext": [{"elem_idx": 0, "mod_idx": 2}], "cor": []},
        ],
        "vnd": [
            {"ext": [], "cor": [{"elem_idx": 0, "mod_idx": 0}]},
        ],
    },
    {
        "sig": [
            {"ext": [{"elem_idx": 0, "mod_idx": 1}], "cor": [{"elem_idx": 0, "mod_idx": 1}]},
        ]
    }
]

def get_vnd_offset(comp, mod):
    elem_idx = mod['elem_idx']
    mod_idx = mod['mod_idx']
    elem = comp[elem_idx]
    sig = elem['sig']

    if sig is not None and mod_idx > len(sig):
        return len(sig)

    return 0

# Preprocess composition array
for elem_idx, elem in enumerate(comp):
    sig, vnd = elem.get('sig', []), elem.get('vnd', [])
    vnd_offset = len(sig)

    for model_idx, model in enumerate(sig):
        model['elem_idx'] = elem_idx
        model['mod_idx'] = model_idx
        model['vnd_offset'] = vnd_offset

        if 'cor' in model:
            for cor in model['cor']:
                cor['vnd_offset'] = get_vnd_offset(comp, cor)

                bt_mesh.bt_mesh_model_correspond(cor, model)

        if 'ext' in model:
            for ext in model['ext']:
                ext['vnd_offset'] = get_vnd_offset(comp, ext)
                bt_mesh.bt_mesh_model_extend(ext, model)

# Generate page 1 binary data
page_data = bt_mesh.bt_mesh_comp_data_get_page_1(comp)
print(page_data)

