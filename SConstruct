import os

SetOption('num_jobs', max(1, int((os.cpu_count() or 1)-1)))

# Index firmware TUs and board/lwip sources for clangd. SCons CompilationDatabase
# only records linked objects, which misses the lwIP tree (and panda is a unity build).
import generate_compile_commands
generate_compile_commands.main()

# panda fw & test files
SConscript('SConscript')
