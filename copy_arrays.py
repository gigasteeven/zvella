import re

with open('d:/claude/layoutmode/XDBotFork-XDBot/src/hacks/layout_mode.hpp', 'r') as f:
    hpp = f.read()

out = '\nnamespace XDBot {\n'

arrays = re.findall(r'const std::(?:unordered_set|map)<[^>]+>\s+\w+\s*=\s*\{[^}]+\};', hpp)
for arr in arrays:
    out += arr + '\n\n'

out += 'const std::unordered_set<int> shaderIDs = {2904, 2905, 2907, 2909, 2910, 2911, 2912, 2913, 2914, 2915, 2916, 2917, 2919, 2920, 2921, 2922, 2923, 2924};\n\n'

out += '}\n'

with open('d:/claude/layoutmode/layout-obs-bypass/src/XDBotLayout.cpp', 'a') as f:
    f.write(out)
