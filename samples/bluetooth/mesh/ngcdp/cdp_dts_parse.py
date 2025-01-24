import os
import tempfile
import devicetree.edtlib as edtlib

# Path to your DTS file
dts_file = "ble_mesh.overlay"
#dts_file = "build/ngcdp/zephyr/zephyr.dts"

# Read the original content
with open(dts_file, "r") as original_file:
    original_content = original_file.read()

# Prepend "/dts-v1/;" and create an temporary file
# "/dts-v1/;" is required for the edtlib.EDT to work
with tempfile.NamedTemporaryFile(mode="w+", delete=False, suffix=".dts") as temp_file:
    temp_file.write("/dts-v1/;\n" + original_content)
    temp_file_path = temp_file.name

# Path to the include directories (if applicable)
# Usually, the Zephyr project or your specific project includes these.

# All bindings-dirs in Zephyr can be found in build/<app_name>/build_info.yml
include_dirs = ['/home/pavel/nordic/ncs/git-repos/ncs/nrf/dts/bindings',
                '/home/pavel/nordic/ncs/git-repos/ncs/nrf/samples/bluetooth/mesh/ngcdp/dts/bindings',
                '/home/pavel/nordic/ncs/git-repos/ncs/zephyr/dts/bindings']

# Create an EDT instance
try:
    #edt = edtlib.EDT(dts_file, include_dirs)
    edt = edtlib.EDT(temp_file_path, include_dirs)
except edtlib.EDTError as e:
    print(f"EDT Error: {e}")
    exit(1)

os.remove(temp_file_path)

# Access and process the parsed data
def parse_mesh_nodes(edt):
    comp = list()

    # Find the mesh node
    mesh_node = None

    for node in edt.nodes:
        if node.name == "mesh":
            mesh_node = node
            break

    if mesh_node is None:
        print("Mesh node not found!")
        return

    print(f"Parsing 'mesh' node: {mesh_node.path}")

    # Find elements within the mesh
    elements = mesh_node.children['elements']
    for element_name, element_node in elements.children.items():
        print(f"\nElement: {element_name}")
        print(f"  Location: {element_node.props.get('location').val}")
        print(f"  Compatible: {element_node.props.get('compatible').val}")

        parsed_models = dict()
        sig_models = list()

        # Access models within the element
        models = element_node.children['models']
        for model_name, model_node in models.children.items():
            print(f"    Model: {model_name}")
            for prop_name, prop in model_node.props.items():
                print(f"      {prop_name}: {prop.val}")

            # Add the model to the list of SIG models
            sig_models.append({"ext":[],"cor":[]})

        parsed_models["sig"] = sig_models
        # Can also be skipped
        parsed_models["vnd"] = list()

        # Add the element to the composition
        comp.append(parsed_models)

    return comp

# Call the parser function
comp = parse_mesh_nodes(edt)

# Print the composition
print(comp)
