import subprocess ,re
cmd=["clang-tidy","ldd.c","-p",".","--extra-arg=-I/lib/modules/$(uname -r)/build/include","-export-fixes=tidy_fixes.yaml"]
out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
text = out.stdout
# crude counts; good enough to start
warnings = len(re.findall(r":\d+:\d+:\s+warning:", text))
errors   = len(re.findall(r":\d+:\d+:\s+error:", text))
notes    = len(re.findall(r":\d+:\d+:\s+note:", text))

print(f"Number of warnings:{warnings}")
print(f"Number of errors:{errors}")
print(f"Number of notes:{notes}")