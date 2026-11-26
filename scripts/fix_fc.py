with open('/app/CAS/src/kernel/elaborator.cpp') as f:
    text = f.read()

target1 = "for (const auto& fc : pkg->forward_classes) {"
replacement1 = """for (const auto& fc : pkg->forward_classes) {
            if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;"""

target2 = "for (const auto& fc : mod->forward_classes) {"
replacement2 = """for (const auto& fc : mod->forward_classes) {
            if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;"""

target3 = "for (const auto& fc : Parser::global_forward_classes) {"
replacement3 = """for (const auto& fc : Parser::global_forward_classes) {
        if (fc == "process" || fc == "semaphore" || fc == "mailbox") continue;"""

print("Counts:", text.count(target1), text.count(target2), text.count(target3))
text = text.replace(target1, replacement1)
text = text.replace(target2, replacement2)
text = text.replace(target3, replacement3)
with open('/app/CAS/src/kernel/elaborator.cpp', 'w') as f:
    f.write(text)
print("SUCCESS")
