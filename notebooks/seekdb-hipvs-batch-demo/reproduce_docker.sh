#!/usr/bin/env bash
# Run on a Linux AMD GPU host with Docker Engine and ROCm device access.
set -euo pipefail

image="${1:-crpi-a7t9nblyxh55vyd2.cn-shanghai.personal.cr.aliyuncs.com/muzihao2/work:seekdb-hipvs-nb-demo}"
name="${3:-seekdb-hipvs-course-reproduction}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace="${2:-$script_dir/notebook_workspace}"

if ! command -v docker >/dev/null 2>&1; then
  echo 'Docker CLI is missing. Run this on a Linux AMD GPU host with Docker Engine.' >&2
  exit 127
fi
docker info >/dev/null
if docker container inspect "$name" >/dev/null 2>&1; then
  echo "Container $name already exists; choose a different name." >&2
  exit 1
fi
if [[ ! -c /dev/kfd ]]; then
  echo 'ROCm compute device /dev/kfd is missing.' >&2
  exit 1
fi
render=''
for candidate in /dev/dri/renderD135 /dev/dri/renderD*; do
  if [[ -c "$candidate" ]]; then
    render="$candidate"
    break
  fi
done
if [[ -z "$render" ]]; then
  echo 'No GPU render device was found under /dev/dri.' >&2
  exit 1
fi

mkdir -p -- "$workspace"
workspace="$(cd -- "$workspace" && pwd)"
project="$workspace/course-reproduction"
if [[ -e "$project" ]]; then
  echo "Use a fresh workspace; refusing to overwrite $project" >&2
  exit 1
fi
mkdir -p -- "$project/lib"
for file in seekdb_hipvs_batch_demo.ipynb run_demo.py verify_course_paths.py README.md requirements.txt NOTEBOOK_CELLS_CN.md LICENSE.course; do
  cp -- "$script_dir/$file" "$project/$file"
done
for file in seekdb_demo.py start_server.py sdb.py; do
  cp -- "$script_dir/lib/$file" "$project/lib/$file"
done

docker pull "$image"
docker image inspect "$image" > "$workspace/docker-image.json"
image_id="$(docker image inspect --format '{{.Id}}' "$image")"
docker run -d --name "$name" \
  --device=/dev/kfd --device="$render" \
  --group-add "$(stat -c '%g' /dev/kfd)" \
  --group-add "$(stat -c '%g' "$render")" \
  --security-opt seccomp=unconfined \
  -p 127.0.0.1:18890:8888 \
  -v "$workspace:/workspace" \
  "$image_id" > "$workspace/container-id.txt"

docker exec "$name" bash -lc '
  set -e
  cd /workspace/course-reproduction
  python_bin=python
  if test -x /opt/venv/bin/python; then python_bin=/opt/venv/bin/python; fi
  "$python_bin" run_demo.py --output results/docker-reproduction/executed.ipynb
  cp results/summary.json results/summary.csv results/seekdb_hipvs_speedup.png results/docker-reproduction/
  "$python_bin" verify_course_paths.py --output results/docker-reproduction/path-checks.json
'
printf 'Results: %s/course-reproduction/results/docker-reproduction\n' "$workspace"
printf 'Jupyter: http://127.0.0.1:18890 (use the course image login configuration)\n'
