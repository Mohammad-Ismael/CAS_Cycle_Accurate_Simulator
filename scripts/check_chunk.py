
with open('/app/CAS/src/kernel/elaborator.cpp') as f:
    text = f.read()

s1 = 'uint32_t tw = resolve_type_width(ov_t);'
print('Count of resolve_type_width(ov_t):', text.count(s1))
s2 = 'uint32_t tw = resolve_type_width(def_t);'
print('Count of resolve_type_width(def_t):', text.count(s2))
