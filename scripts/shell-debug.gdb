set pagination off
set confirm off
set architecture i386:x86-64

file build/kernel.elf
target remote :1234

echo \nConnected. Setting shell breakpoints...\n
break shell_main
break shell_execute
break shell_spawn_command

echo Breakpoints ready. Use 'continue' to run.\n
