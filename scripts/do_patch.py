with open('/app/CAS/src/kernel/elaborator.cpp') as f:
    text = f.read()

target1 = "uint32_t tw = resolve_type_width(ov_t);"
replacement1 = """uint32_t tw = 0;
                            const PackedStructType* st_ov = find_struct_type(ov_t, parent_scope);
                            if (!st_ov) st_ov = find_struct_type(ov_t, scope);
                            if (st_ov) tw = st_ov->total_width;
                            else tw = resolve_type_width(ov_t);"""

target2 = "uint32_t tw = resolve_type_width(def_t);"
replacement2 = """uint32_t tw = 0;
                        const PackedStructType* st_def = find_struct_type(def_t, scope);
                        if (!st_def) st_def = find_struct_type(def_t, parent_scope);
                        if (st_def) tw = st_def->total_width;
                        else tw = resolve_type_width(def_t);"""

print("Counts before:", text.count(target1), text.count(target2))
if text.count(target1) == 2 and text.count(target2) == 1:
    text = text.replace(target1, replacement1)
    text = text.replace(target2, replacement2)
    with open('/app/CAS/src/kernel/elaborator.cpp', 'w') as f:
        f.write(text)
    print("SUCCESS")
else:
    print("COUNT MISMATCH")
