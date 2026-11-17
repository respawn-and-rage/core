#!/usr/bin/env python3
"""Embed the Lambda source in a deployment template without an S3 artifact bucket."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent


def main():
    source = (ROOT / "app.py").read_text(encoding="utf-8")
    compile(source, "app.py", "exec")
    template = (ROOT / "stack.yaml").read_text(encoding="utf-8")
    print(template.replace("!!LAMBDA_SOURCE!!", json.dumps(source, ensure_ascii=False)), end="")


if __name__ == "__main__":
    main()
