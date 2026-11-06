with open('/app/projects/ibex/examples/sw/simple_system/hello_test/hello_test.vmem') as f:
    words = [w for line in f if not line.startswith('@') for w in line.split()]
print('32:', words[32])
print('210-215:', words[210:215])
