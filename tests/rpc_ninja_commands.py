"""Test rebuild command parsing. GPL-2.0-only; no shell execution.

Windows uses the MS C-runtime rules documented by subprocess.list2cmdline:
backslashes are literal except immediately before a double quote. POSIX
shlex with escapes disabled loses the quotes in Ninja's -DNAME=\"value\".
"""
import shlex

def split_command(command, windows=False):
    if not windows:
        return shlex.split(command)
    result=[];i=0
    while i<len(command):
        while i<len(command) and command[i] in ' \t':i+=1
        if i==len(command):break
        word=[];quoted=False
        while i<len(command):
            if command[i] in ' \t' and not quoted:break
            if command[i]=='\\':
                start=i
                while i<len(command) and command[i]=='\\':i+=1
                count=i-start
                if i<len(command) and command[i]=='"':
                    word.extend('\\'*(count//2))
                    if count%2:word.append('"');i+=1;continue
                else:
                    word.extend('\\'*count);continue
            if command[i]=='"':
                if quoted and i+1<len(command) and command[i+1]=='"':
                    word.append('"');i+=2;continue
                quoted=not quoted;i+=1
            else:word.append(command[i]);i+=1
        result.append(''.join(word))
    return result
