#!/usr/bin/env python3
"""
ScreenSchema OTA Server
Serves firmware manifests and binaries for OTA updates.
"""
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory, abort

app = Flask(__name__)
FIRMWARE_ROOT = Path(os.environ.get("FIRMWARE_ROOT", "/firmware"))
PUBLIC_BASE_URL = os.environ.get("PUBLIC_BASE_URL", "http://localhost:3333")


def manifest_path(board_id: str) -> Path:
    return FIRMWARE_ROOT / board_id / "manifest.json"


def firmware_path(board_id: str) -> Path:
    return FIRMWARE_ROOT / board_id / "firmware.bin"


@app.get("/manifest/<board_id>")
def get_manifest(board_id):
    mf = manifest_path(board_id)
    if not mf.exists():
        abort(404, description=f"No manifest for board: {board_id}")
    return jsonify(json.loads(mf.read_text()))


@app.get("/firmware/<board_id>/<filename>")
def get_firmware(board_id, filename):
    board_dir = FIRMWARE_ROOT / board_id
    if not board_dir.exists():
        abort(404)
    return send_from_directory(str(board_dir), filename)


@app.post("/publish/<board_id>")
def publish(board_id):
    if "firmware" not in request.files:
        abort(400, description="Missing 'firmware' file field")
    version = request.form.get("version", "").strip()
    if not version:
        abort(400, description="Missing 'version' form field")

    board_dir = FIRMWARE_ROOT / board_id
    board_dir.mkdir(parents=True, exist_ok=True)

    fw_file = request.files["firmware"]
    fw_bytes = fw_file.read()
    fw_path = board_dir / "firmware.bin"
    fw_path.write_bytes(fw_bytes)

    sha256 = hashlib.sha256(fw_bytes).hexdigest()
    fw_url = f"{PUBLIC_BASE_URL}/firmware/{board_id}/firmware.bin"
    published_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    manifest = {
        "version": version,
        "board": board_id,
        "firmware_url": fw_url,
        "size": len(fw_bytes),
        "sha256": sha256,
        "published_at": published_at,
    }
    manifest_path(board_id).write_text(json.dumps(manifest, indent=2))

    return jsonify({"ok": True, "manifest": manifest}), 201


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=3333, debug=True)
