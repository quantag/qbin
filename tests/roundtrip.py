#!/usr/bin/env python3
import argparse, subprocess, sys, os, shutil, difflib, pathlib

def run(cmd, cwd=None):
  p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
  return p.returncode, p.stdout, p.stderr

def read_bytes(p): return pathlib.Path(p).read_bytes()
def read_text(p): return pathlib.Path(p).read_text(encoding="utf-8", errors="replace")

def normalize(s: str) -> str:
  # Strip trailing whitespace on lines and ensure single newline at EOF
  lines = [ln.rstrip() for ln in s.splitlines() if True]
  return "\n".join(lines) + "\n"

def main():
  ap = argparse.ArgumentParser(description="QBIN round-trip tester (QASM -> QBIN -> QASM)")
  ap.add_argument("--compiler", required=True, help="path to qbin-compile")
  ap.add_argument("--decompiler", required=True, help="path to qbin-decompile")
  ap.add_argument("--qasm", required=True, help="input .qasm file")
  ap.add_argument("--workdir", required=True, help="work directory for artifacts")
  ap.add_argument("--exact", action="store_true", help="require byte-for-byte equality")
  ap.add_argument("--keep", action="store_true", help="keep workdir on success")
  args = ap.parse_args()

  qasm_in = os.path.abspath(args.qasm)
  work = os.path.abspath(args.workdir)
  os.makedirs(work, exist_ok=True)
  qbin = os.path.join(work, "out.qbin")
  qasm_out = os.path.join(work, "out.qasm")

  # Compile
  rc, so, se = run([args.compiler, qasm_in, "-o", qbin], cwd=work)
  if rc != 0:
    sys.stderr.write("Compiler failed (rc={}):\n{}\n{}\n".format(rc, so, se))
    return 1

  # Decompile
  rc, so, se = run([args.decompiler, qbin, "-o", qasm_out], cwd=work)
  if rc != 0:
    sys.stderr.write("Decompiler failed (rc={}):\n{}\n{}\n".format(rc, so, se))
    return 1

  # Compare
  in_bytes = read_bytes(qasm_in)
  out_bytes = read_bytes(qasm_out)

  if args.exact:
    if in_bytes == out_bytes:
      if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
      print("OK (exact) -", os.path.basename(qasm_in))
      return 0
    else:
      # Show a unified diff for readability
      a = read_text(qasm_in).splitlines()
      b = read_text(qasm_out).splitlines()
      diff = "\n".join(difflib.unified_diff(a, b, fromfile="input", tofile="decompiled", lineterm=""))
      sys.stderr.write("Mismatch (exact). Unified diff:\n{}\n".format(diff))
      return 2
  else:
    # Normalized comparison
    if normalize(read_text(qasm_in)) == normalize(read_text(qasm_out)):
      if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
      print("OK (normalized) -", os.path.basename(qasm_in))
      return 0
    else:
      a = normalize(read_text(qasm_in)).splitlines()
      b = normalize(read_text(qasm_out)).splitlines()
      diff = "\n".join(difflib.unified_diff(a, b, fromfile="input(norm)", tofile="decompiled(norm)", lineterm=""))
      sys.stderr.write("Mismatch (normalized). Unified diff:\n{}\n".format(diff))
      return 3

if __name__ == "__main__":
  sys.exit(main())
