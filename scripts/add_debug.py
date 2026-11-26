with open('/app/CAS/src/kernel/elaborator.cpp') as f:
    text = f.read()

target = "for (auto& p : mod->ports) {"
replacement = """if (scope.find("i_frontend") != std::string::npos) {
        std::cerr << "[FRONTEND_PORT_DEBUG] scope=" << scope << "\\n";
        for (const auto& param : mod->parameters) {
            std::cerr << "   param: " << param.name << " type=" << param.type_name << " is_local=" << param.is_localparam << "\\n";
        }
    }
    for (auto& p : mod->ports) {
        if (scope.find("i_frontend") != std::string::npos) {
            std::cerr << "   port: " << p.name << " type=" << p.type_name << " is_signed=" << p.is_signed << "\\n";
        }"""

print("Target count:", text.count(target))
text = text.replace(target, replacement, 1)
with open('/app/CAS/src/kernel/elaborator.cpp', 'w') as f:
    f.write(text)
print("SUCCESS")
