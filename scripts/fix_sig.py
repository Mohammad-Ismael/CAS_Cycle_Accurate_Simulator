with open('/app/CAS/src/kernel/elaborator.cpp') as f:
    text = f.read()

target = "LogicVal Elaborator::eval_const_logic_val(ExpressionNode* expr, const std::string& scope) {"
replacement = "LogicVal Elaborator::eval_const_logic_val(ExpressionNode* expr, const std::string& scope, const std::string& target_type_name) {"

print("Target count:", text.count(target))
text = text.replace(target, replacement, 1)
with open('/app/CAS/src/kernel/elaborator.cpp', 'w') as f:
    f.write(text)
print("SUCCESS")
