"""Create local import libraries for the host's tier0 and vstdlib DLLs."""
from pathlib import Path
import os
import subprocess
import pefile

root = Path(__file__).resolve().parent.parent
host = Path(os.environ.get('OPGLUA_HOST_BIN', 'D:/Program Files (x86)/Steam/steamapps/common/GarrysMod/bin/win64'))
output = root / 'build/windows/imports'
output.mkdir(parents=True, exist_ok=True)
for module in ['tier0', 'vstdlib']:
    dll = host / (module + '.dll')
    pe = pefile.PE(str(dll))
    lines = ['LIBRARY ' + dll.name, 'EXPORTS']
    for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if not symbol.name:
            continue
        name = symbol.name.decode('ascii')
        section = pe.get_section_by_rva(symbol.address)
        data = section is not None and not (section.Characteristics & 0x20000000)
        lines.append(name + (' DATA' if data else ''))
    definition = output / (module + '.def')
    definition.write_text('\n'.join(lines) + '\n', encoding='ascii')
    subprocess.run(['lib', '/nologo', '/machine:x64', '/def:' + str(definition),
                    '/out:' + str(output / (module + '.lib'))], check=True)
print('Prepared local host import libraries.')
