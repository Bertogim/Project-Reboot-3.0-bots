#!/usr/bin/bash
#
# build-vm-internal.sh - Compilacion remota en la VM de Windows (VirtualBox) por
# VBoxManage guestcontrol, para VMs en Red Interna (sin SSH).
#
# Port de build-vm.sh: misma estructura y flujo, reemplazando ssh/scp por
# VBoxManage guestcontrol (run/copyto/copyfrom).
#
#  1) Empaqueta el codigo (dir del proyecto + vendor) en un .tar.gz
#  2) Lo sube a la VM (copyto) y lo descomprime en C:\build
#  3) Lanza MSBuild de forma despegada (WMI, sobrevive a cerrar la sesion)
#  4) Espera a que termine y baja el DLL (copyfrom) a Build/
#
#  Uso:  ./build-vm-internal.sh [--clean]
#
#  Config: lee vm-config.env (o vm-config.env.example) con los datos de la VM.
#          Las variables de entorno (VM_NAME, VM_USER, ...) tienen prioridad.
#
#  Requisitos: VM encendida con Guest Additions y cuenta Windows con acceso.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- config (plantilla o real, entorno por encima) ----------------------------
CFG=""
if [ -f "$SCRIPT_DIR/vm-config.env" ]; then CFG="$SCRIPT_DIR/vm-config.env"; fi
if [ -f "$SCRIPT_DIR/vm-config.env.example" ]; then CFG="${CFG:-$SCRIPT_DIR/vm-config.env.example}"; fi
if [ -n "$CFG" ]; then
    # shellcheck disable=SC1090
    set -a; source "$CFG"; set +a
fi

VM_NAME="${VM_NAME:-Windows lite}"
VM_USER="${VM_USER:-admin}"
VM_PASS="${VM_PASS:-}"
VM_BUILD_DIR="${VM_BUILD_DIR:-C:/build}"
VM_SRC_DIR="${VM_SRC_DIR:-C:/build/Project Reboot 3.0}"
VM_OUT_DIR="${VM_OUT_DIR:-C:/build/out}"

PS_EXE="C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"
MAX_WAIT="${VM_MAX_WAIT:-2400}"   # segundos tope esperando el build
REMOTE_EXIT_MARK="${VM_BUILD_DIR}/msbuild_exit.txt"
REMOTE_LOG="${VM_BUILD_DIR}/msbuild_out.txt"
LOCAL_OUT='Build'                 # destino local del DLL (ya en .gitignore)

usage() {
    echo "Uso: $0 [--clean]"
    echo "  --clean  borra objetos intermedios (x64, out) en la VM antes de compilar"
    echo
    echo "Config en vm-config.env (ver la plantilla vm-config.env.example)."
    echo "La VM se selecciona por nombre (VM_NAME, default 'Windows lite')."
    exit 0
}
CLEAN=0
case "${1:-}" in
    --clean) CLEAN=1 ;;
    --help|-h) usage ;;
esac

for c in VBoxManage tar gzip sed grep; do command -v "$c" >/dev/null || { echo "Falta: $c" >&2; exit 1; }; done

# --- helpers -----------------------------------------------------------------
say() { printf '[build-vm-internal] %s\n' "$*" >&2; }
die() { say "ERROR: $*" >&2; exit 1; }
[ -n "$VM_PASS" ] || die "VM_PASS vacio; revisa vm-config.env"

guest_ps() {   # guest_ps <poder-shell-command>
    VBoxManage guestcontrol "$VM_NAME" run \
        --exe "$PS_EXE" \
        --username "$VM_USER" --password "$VM_PASS" \
        -- -NoProfile -Command "$1"
}

vm_ok() {
    VBoxManage list runningvms 2>/dev/null | grep -q "\"$VM_NAME\""
}

vm_is_building() {
    guest_ps "(Get-Process MSBuild -ErrorAction SilentlyContinue) -ne \$null" 2>/dev/null | grep -q True
}

wait_vm_idle() {
    say "comprobando que la VM no este compilando..."
    local waited=0
    while vm_is_building; do
        waited=$((waited + 10))
        [ "$waited" -ge "$MAX_WAIT" ] && die "hay un build corriendo y no termino en ${MAX_WAIT}s; abortando"
        say "  build en curso... (${waited}s)"
        sleep 10
    done
}

say "comprobando acceso guestcontrol a la VM '$VM_NAME'..."
vm_ok || die "la VM '$VM_NAME' no esta encendida"
guest_ps "whoami" >/dev/null 2>&1 || die "no hay acceso guestcontrol a la VM '$VM_NAME' (revisa usuario/pass y Guest Additions)"
wait_vm_idle

# --- 1. paquete local ---------------------------------------------------------
T="$(mktemp -d /tmp/opencode/reboot-build.XXXXXX)"
trap 'rm -rf "$T"' EXIT

say "empaquetando codigo (proyecto + vendor)..."
tar -czf "$T/src.tar.gz" "Project Reboot 3.0" vendor
say "  $(du -h "$T/src.tar.gz" | cut -f1)"

cat > "$T/build.bat" <<'EOF'
@echo off
cd /d "C:\build\Project Reboot 3.0"
set TARGET=%~1
if /i "%TARGET%"=="clean" goto :clean
"C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "Project Reboot 3.0.vcxproj" /p:configuration=Release /p:platform=x64 /p:OutDir=C:\build\out\ /m /fl /flp:logfile=C:\build\msbuild_out.txt;verbosity=normal /v:q /nologo > C:\build\msbuild_console.txt 2>&1
echo EXITCODE=%ERRORLEVEL% > C:\build\msbuild_exit.txt
exit /b %ERRORLEVEL%
:clean
for /d %%D in ("C:\build\Project Reboot 3.0\Project .*") do rd /s /q "%%D" 2>nul
for /d %%D in ("C:\build\Project Reboot 3.0\x64") do rd /s /q "%%D" 2>nul
if exist "C:\build\out" rd /s /q "C:\build\out"
mkdir "C:\build\out" >nul 2>&1
echo limpieza de intermedios completada > C:\build\msbuild_clean.txt
echo CLEAN_EXITCODE=%ERRORLEVEL% > C:\build\msbuild_clean_exit.txt
exit /b 0
EOF

cat > "$T/launch_build.ps1" <<'EOF'
$cmd = 'cmd.exe /c C:\build\build.bat'
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd } | Select-Object ProcessId, ReturnValue
EOF

# --- 2. subir -----------------------------------------------------------------
say "subiendo codigo y scripts a la VM ($VM_NAME)..."
VBoxManage guestcontrol "$VM_NAME" copyto --username "$VM_USER" --password "$VM_PASS" "$T/src.tar.gz"          "${VM_BUILD_DIR}/reboot_src.tar.gz"   >/dev/null 2>&1
VBoxManage guestcontrol "$VM_NAME" copyto --username "$VM_USER" --password "$VM_PASS" "$T/build.bat"           "${VM_BUILD_DIR}/build.bat"            >/dev/null 2>&1
VBoxManage guestcontrol "$VM_NAME" copyto --username "$VM_USER" --password "$VM_PASS" "$T/launch_build.ps1"    "${VM_BUILD_DIR}/launch_build.ps1"     >/dev/null 2>&1

if [ "$CLEAN" -eq 1 ]; then
    say "limpiando objetos intermedios en la VM (MSBuild /t:Clean)..."
    VBoxManage guestcontrol "$VM_NAME" run --exe "cmd.exe" --username "$VM_USER" --password "$VM_PASS" -- /c "call \"C:\build\build.bat\" clean & echo CLEAN_DONE" >/dev/null 2>&1
    clean_rc="$(guest_ps "Get-Content 'C:/build/msbuild_clean_exit.txt' -ErrorAction SilentlyContinue" 2>/dev/null | tr -d '\r' || true)"
    if ! echo "$clean_rc" | grep -q 'CLEAN_EXITCODE=0'; then
        say "detalle de /t:Clean:"
        guest_ps "Get-Content 'C:/build/msbuild_clean.txt' -Tail 20" 2>/dev/null || true
        die "fallo la limpieza (${clean_rc:-sin marcador})"
    fi
fi

say "descomprimiendo en la VM..."
guest_ps "Remove-Item '${REMOTE_EXIT_MARK}' -ErrorAction SilentlyContinue; tar -xf 'C:/build/reboot_src.tar.gz' -C 'C:/build'; Write-Host EXTRACT_OK" 2>/dev/null | grep -q EXTRACT_OK || die "fallo al descomprimir en la VM"

# --- 3. compilar --------------------------------------------------------------
say "lanzando MSBuild (despegado, WMI)..."
VBoxManage guestcontrol "$VM_NAME" run --exe "$PS_EXE" --username "$VM_USER" --password "$VM_PASS" -- -NoProfile -ExecutionPolicy Bypass -File "${VM_BUILD_DIR}\launch_build.ps1" >/dev/null 2>&1

say "esperando que termine el build (max ${MAX_WAIT}s)..."
waited=0
while :; do
    code="$(guest_ps "Get-Content '${REMOTE_EXIT_MARK}' -ErrorAction SilentlyContinue" 2>/dev/null | tr -d '\r' || true)"
    if echo "$code" | grep -q 'EXITCODE='; then
        build_rc="$(echo "$code" | sed -n 's/.*EXITCODE=\([0-9]*\).*/\1/p' | head -1)"
        break
    fi
    waited=$((waited + 5))
    if [ $((waited % 20)) -eq 0 ]; then
        size="$(guest_ps "(Get-Item '${REMOTE_LOG}' -ErrorAction SilentlyContinue).Length" 2>/dev/null | tr -d '\r' || true)"
        say "  ...esperando (${waited}s, log ${size:-0}B)"
    fi
    if [ "$waited" -ge "$MAX_WAIT" ]; then
        say "timeout esperando al build; ultimas lineas del log:"
        guest_ps "Get-Content '${REMOTE_LOG}' -Tail 20" 2>/dev/null || true
        die "el build no termino en ${MAX_WAIT}s"
    fi
    sleep 5
done

say "msbuild salio con codigo: $build_rc"
if [ "$build_rc" != "0" ]; then
    say "errores del build:"
    guest_ps "Get-Content '${REMOTE_LOG}' | Select-String -Pattern ': error ' | Select-Object -Last 40" 2>/dev/null || true
    say "ultimas lineas del log:"
    guest_ps "Get-Content '${REMOTE_LOG}' -Tail 60" 2>/dev/null || true
    die "la compilacion fallo (log completo en ${REMOTE_LOG})"
fi

# --- 4. bajar el resultado ----------------------------------------------------
say "empaquetando y bajando el DLL..."
guest_ps "tar -cf 'C:/build/out.tar' -C 'C:/build' out; Write-Host TAR_OK" 2>/dev/null | grep -q TAR_OK || die "fallo al empaquetar out en la VM"
VBoxManage guestcontrol "$VM_NAME" copyfrom --username "$VM_USER" --password "$VM_PASS" "${VM_BUILD_DIR}/out.tar" "$T/out.tar" >/dev/null 2>&1

mkdir -p "$LOCAL_OUT"
tar -xf "$T/out.tar" --strip-components=1 -C "$LOCAL_OUT"

say "compilacion completa. Artefactos en '${LOCAL_OUT}/':"
ls -la "$LOCAL_OUT"