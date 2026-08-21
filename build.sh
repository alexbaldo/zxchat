#!/usr/bin/env bash
# ZXChat - asistente de compilacion.
#
# Pregunta idioma, maquina, proveedor, modelo y clave, y deja un .tap listo.
# La clave NO se guarda: se escribe en secrets.h para compilar y se borra al
# terminar, pase lo que pase (trap EXIT).
set -uo pipefail

RAIZ="$(cd "$(dirname "$0")" && pwd -P)"
SECRETS="$RAIZ/spectrum/src/secrets.h"

# ------------------------------------------------------------------ estilo
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    R=$'\033[0m'; N=$'\033[1m'; D=$'\033[2m'
    AZ=$'\033[38;5;39m'; VE=$'\033[38;5;42m'; AM=$'\033[38;5;220m'
    RO=$'\033[38;5;203m'; MA=$'\033[38;5;170m'; CI=$'\033[38;5;51m'
    # El logo, con los colores que pinta el Spectrum: blanco, rojo, amarillo,
    # verde y cian, todos brillantes.
    LB=$'\033[38;5;231m'; LR=$'\033[38;5;196m'; LY=$'\033[38;5;226m'
    LG=$'\033[38;5;46m';  LC=$'\033[38;5;51m'
else
    R=; N=; D=; AZ=; VE=; AM=; RO=; MA=; CI=
    LB=; LR=; LY=; LG=; LC=
fi

logo() {
    printf '\n'
    printf '  %s███████ %s██   ██ %s ██████ %s██   ██ %s █████  %s███████%s\n' "$LB" "$LB" "$LR" "$LY" "$LG" "$LC" "$R"
    printf '  %s     ██ %s ██ ██  %s██      %s██   ██ %s██   ██ %s   ██  %s\n' "$LB" "$LB" "$LR" "$LY" "$LG" "$LC" "$R"
    printf '  %s   ███  %s  ███   %s██      %s███████ %s███████ %s   ██  %s\n' "$LB" "$LB" "$LR" "$LY" "$LG" "$LC" "$R"
    printf '  %s  ██    %s ██ ██  %s██      %s██   ██ %s██   ██ %s   ██  %s\n' "$LB" "$LB" "$LR" "$LY" "$LG" "$LC" "$R"
    printf '  %s███████ %s██   ██ %s ██████ %s██   ██ %s██   ██ %s   ██  %s\n' "$LB" "$LB" "$LR" "$LY" "$LG" "$LC" "$R"
    printf '%s        an AI chatbot that runs on a ZX Spectrum%s\n\n' "$D" "$R"
}

titulo() { printf '\n%s%s%s\n%s%s%s\n' "$N" "$1" "$R" "$D" "$(printf '%*s' ${#1} '' | tr ' ' '-')" "$R"; }
aviso()  { printf '%s  !%s %s\n' "$AM" "$R" "$1"; }
malo()   { printf '%s  x%s %s\n' "$RO" "$R" "$1"; }
bien()   { printf '%s  v%s %s\n' "$VE" "$R" "$1"; }

# barra(hecho, total, texto)
barra() {
    local h=$1 t=$2 txt=$3 ancho=32 i lleno
    lleno=$(( h * ancho / t ))
    printf '\r  %s' "$AZ"
    for ((i=0;i<ancho;i++)); do [ $i -lt $lleno ] && printf '#' || printf "${D}.${AZ}"; done
    printf '%s %3d%%  %-28s' "$R" $(( h * 100 / t )) "$txt"
    [ "$h" -eq "$t" ] && printf '\n'
}

# ------------------------------------------------------------------ idioma
IDIOMA=en
t() {
    local k=$1
    if [ "$IDIOMA" = es ]; then
        case $k in
        lang)     echo "Idioma de ZXChat en el Spectrum";;
        machine)  echo "Modelo de Spectrum";;
        provider) echo "Proveedor de IA";;
        model)    echo "Modelo";;
        key)      echo "Clave de API";;
        keyhelp)  echo "Se usa solo para compilar y no se guarda en ningún sitio.";;
        keyget)   echo "Consíguela en:";;
        keypaste) echo "Pégala aquí (no se verá al escribir): ";;
        keybad)   echo "Eso no parece una clave válida. Prueba otra vez.";;
        keyok)    echo "Clave recibida";;
        building) echo "Compilando";;
        built)    echo "Listo:";;
        upload)   echo "He encontrado un Spectranext conectado. ¿Lo subo? [S/n] ";;
        sent)     echo "Enviado al cartucho como";;
        load)     echo "Cárgalo desde BASIC con:";;
        nospx)    echo "No veo ningún Spectranext conectado.";;
        choose)   echo "Elige [1-%d]: ";;
        best)     echo "recomendado";;
        unmeasured) echo "Estas notas son estimaciones: solo los modelos de OpenAI están medidos.";;
        reasoning) echo "Esfuerzo de razonamiento";;
        rnone)    echo "Ninguno   - responde al momento";;
        rlow)     echo "Bajo      - piensa un poco antes de contestar";;
        rmed)     echo "Medio     - más acierto, más espera";;
        rhigh)    echo "Alto      - lo mejor que sabe, y lo más lento";;
        rnote)    echo "Cada nivel son segundos más de espera y más tokens gastados:";;
        rnote2)   echo "medio y alto pueden multiplicar por diez el coste.";;
        uploading) echo "Cargando en el cartucho";;
        next)     echo "Siguientes pasos";;
        copyit)   echo "Copia zxchat.tap al cartucho, con spx o desde";;
        copyit2)  echo "device.spectranext.net, y luego:";;
        hide10)   echo "Y si prefieres que no te deje la línea 10 en el";;
        hide11)   echo "programa, cárgalo en una sola línea, en directo:";;
        soon)     echo "próximamente";;
        nomachine)  echo "Todavía no. El binario cruza \$C000, y en +2A/+3 esa zona";;
        nomachine2) echo "se pagina, así que se perderían 10 KB de código.";;
        noprovider)  echo "Todavía no. La rama de Anthropic está escrita, pero nunca";;
        noprovider2) echo "se ha ejecutado contra una clave real.";;
        esac
    else
        case $k in
        lang)     echo "ZXChat language on the Spectrum";;
        machine)  echo "Spectrum model";;
        provider) echo "AI provider";;
        model)    echo "Model";;
        key)      echo "API key";;
        keyhelp)  echo "Used only to build, never stored anywhere.";;
        keyget)   echo "Get one at:";;
        keypaste) echo "Paste it here (input is hidden): ";;
        keybad)   echo "That does not look like a valid key. Try again.";;
        keyok)    echo "Key received";;
        building) echo "Building";;
        built)    echo "Done:";;
        upload)   echo "Found a connected Spectranext. Upload it? [Y/n] ";;
        sent)     echo "Sent to the cartridge as";;
        load)     echo "Load it from BASIC with:";;
        nospx)    echo "spx missing or no cartridge connected: skipping upload.";;
        choose)   echo "Choose [1-%d]: ";;
        best)     echo "recommended";;
        unmeasured) echo "These ratings are estimates: only the OpenAI models are measured.";;
        reasoning) echo "Reasoning effort";;
        rnone)    echo "None      - answers straight away";;
        rlow)     echo "Low       - thinks a little first";;
        rmed)     echo "Medium    - more accurate, more waiting";;
        rhigh)    echo "High      - its best, and its slowest";;
        rnote)    echo "Each level is seconds more waiting and more tokens spent:";;
        rnote2)   echo "medium and high can multiply the cost by ten.";;
        uploading) echo "Uploading to the cartridge";;
        next)     echo "What's next";;
        copyit)   echo "Copy zxchat.tap to the cartridge, with spx or from";;
        copyit2)  echo "device.spectranext.net, and then:";;
        hide10)   echo "And if you would rather not have line 10 left in";;
        hide11)   echo "your program, load it in one line, in direct mode:";;
        soon)     echo "coming soon";;
        nomachine)  echo "Not yet. The binary crosses \$C000, which is paged on";;
        nomachine2) echo "+2A and +3, so 10 KB of code would vanish.";;
        noprovider)  echo "Not yet. The Anthropic path is written, but it has never";;
        noprovider2) echo "been run against a real key.";;
        esac
    fi
}

# menu "titulo" "op1" "op2" ...  -> deja la eleccion (1..n) en RESP
# Si NOTA trae algo, sale entre las opciones y la pregunta: una advertencia
# impresa antes del titulo parece que pertenece a la seccion anterior.
menu() {
    local tit=$1; shift
    local n=$#
    titulo "$tit"
    local i=1
    for o in "$@"; do printf '  %s%d%s  %s\n' "$N" "$i" "$R" "$o"; i=$((i+1)); done
    [ -n "${NOTA:-}" ] && printf '\n  %s%s%s\n' "$D" "$NOTA" "$R"
    [ -n "${NOTA2:-}" ] && printf '  %s%s%s\n' "$D" "$NOTA2" "$R"
    NOTA=""; NOTA2=""
    printf '\n'
    while :; do
        printf "  $(t choose)" "$n"
        read -r RESP || { printf '\n'; exit 1; }
        [[ "$RESP" =~ ^[0-9]+$ ]] && [ "$RESP" -ge 1 ] && [ "$RESP" -le "$n" ] && break
    done
}

# nota(calidad, velocidad, coste) en bolitas
nota() {
    local v=$1 out=''
    for i in 1 2 3 4 5; do [ $i -le $v ] && out+='#' || out+='.'; done
    printf '%s' "$out"
}

# ------------------------------------------------------------------- SDK
# Nadie recien llegado tiene esto en el entorno, asi que lo buscamos nosotros
# en vez de fallar con "unbound variable" a mitad de la compilacion.
buscar_sdk() {
    local c
    for c in "${SPECTRANEXT_SDK_PATH:-}" \
             /opt/homebrew/opt/spectranext-sdk/libexec \
             /usr/local/opt/spectranext-sdk/libexec \
             "$HOME/spectranext-sdk"; do
        [ -n "$c" ] && [ -f "$c/source.sh" ] && { echo "$c"; return 0; }
    done
    return 1
}

SDK="$(buscar_sdk)" || {
    logo
    malo "Spectranext SDK not found."
    printf '\n    brew install spectranext-sdk\n\n'
    printf '  %sIf it is installed elsewhere, set SPECTRANEXT_SDK_PATH.%s\n\n' "$D" "$R"
    exit 1
}
# shellcheck disable=SC1090
. "$SDK/source.sh"
export SPECTRANEXT_SDK_PATH="${SPECTRANEXT_SDK_PATH:-$SDK}"

# =========================================================== el asistente ===
logo

# --- 1. idioma -------------------------------------------------------------
menu "ZXChat language on the Spectrum" \
     "English" "Español"
[ "$RESP" = 2 ] && IDIOMA=es || IDIOMA=en
LANG_TAP=$IDIOMA

# --- 2. maquina ------------------------------------------------------------
# Vuelve a preguntar en vez de seguir como si nada: elegir algo que no existe
# y que el programa continue igual desconcierta mas que una negativa clara.
while :; do
    menu "$(t machine)" \
         "ZX Spectrum 48K / 128K" \
         "$D+2A / +3  ($(t soon))$R"
    [ "$RESP" = 1 ] && break
    printf '\n'
    aviso "$(t nomachine)"
    printf '    %s\n\n' "$(t nomachine2)"
done

# --- 3. proveedor ----------------------------------------------------------
# Anthropic no se puede elegir todavia: el codigo esta escrito pero nunca se ha
# ejecutado contra una clave real, y ofrecer un camino sin probar como si
# funcionara es peor que no ofrecerlo. El codigo sigue ahi, listo para el dia
# que se pruebe; lo unico cerrado es esta puerta.
while :; do
    menu "$(t provider)" \
         "OpenAI" \
         "${D}Anthropic (Claude)  ($(t soon))$R"
    [ "$RESP" = 1 ] && break
    printf '\n'
    aviso "$(t noprovider)"
    printf '    %s\n\n' "$(t noprovider2)"
done

if [ "$RESP" = 1 ]; then
    PROVEEDOR=openai
    AYUDA_URL="https://platform.openai.com/api-keys"
    PREFIJO="sk-"
    #        id             calidad velocidad coste    fallos/30  $/1000
    MODELOS=("gpt-5.4"            5 5 3
             "gpt-5.6-sol"        5 5 5
             "gpt-5.4-mini"       4 5 2
             "gpt-5.6-terra"      4 5 4
             "gpt-5.6-luna"       3 4 2
             "gpt-5.5"            3 2 5
             "gpt-5.4-nano"       2 5 1)
else
    PROVEEDOR=anthropic
    AYUDA_URL="https://console.anthropic.com/settings/keys"
    PREFIJO="sk-ant-"
    # Sin medir: no he podido pasarles el banco de pruebas. El orden y las
    # notas salen de lo que dice el fabricante, no de datos propios.
    #        id                calidad velocidad coste
    MODELOS=("claude-opus-5"      5 2 5
             "claude-sonnet-5"    4 4 3
             "claude-haiku-4-5"   3 5 1)
fi

# --- 4. modelo -------------------------------------------------------------
titulo "$(t model)"
printf '      %-20s %-8s %-8s %-8s\n' "id" \
       "$( [ "$IDIOMA" = es ] && echo calidad || echo quality)" \
       "$( [ "$IDIOMA" = es ] && echo veloc. || echo speed)" \
       "$( [ "$IDIOMA" = es ] && echo coste || echo cost)"
n=$(( ${#MODELOS[@]} / 4 ))
for ((i=0;i<n;i++)); do
    b=$((i*4))
    # Solo donde hay medicion detras: recomendar un modelo que no he probado
    # seria inventarse el consejo.
    marca=""
    [ $i -eq 0 ] && [ "$PROVEEDOR" = openai ] && marca="  <- $(t best)"
    printf '  %s%d%s   %-20s %s%-8s%s %s%-8s%s %s%-8s%s%s%s%s\n' "$N" $((i+1)) "$R" \
        "${MODELOS[$b]}" \
        "$VE" "$(nota ${MODELOS[$((b+1))]})" "$R" \
        "$AZ" "$(nota ${MODELOS[$((b+2))]})" "$R" \
        "$AM" "$(nota ${MODELOS[$((b+3))]})" "$R" "$VE" "$marca" "$R"
done
[ "$PROVEEDOR" = anthropic ] && printf '\n  %s%s%s\n' "$D" "$(t unmeasured)" "$R"
printf '\n'
while :; do
    printf "  $(t choose)" "$n"
    read -r RESP || { printf '\n'; exit 1; }
    [[ "$RESP" =~ ^[0-9]+$ ]] && [ "$RESP" -ge 1 ] && [ "$RESP" -le "$n" ] && break
done
MODELO="${MODELOS[$(( (RESP-1)*4 ))]}"

# --- 5. razonamiento -------------------------------------------------------
# Solo OpenAI: Anthropic lo pide con otro campo distinto.
RAZONA=""
if [ "$PROVEEDOR" = openai ]; then
    NOTA="$(t rnote)"; NOTA2="$(t rnote2)"
    menu "$(t reasoning)" "$(t rnone)" "$(t rlow)" "$(t rmed)" "$(t rhigh)"
    case "$RESP" in
        2) RAZONA=low;; 3) RAZONA=medium;; 4) RAZONA=high;;
    esac
fi

# --- 6. clave --------------------------------------------------------------
titulo "$(t key)"
printf '  %s\n' "$(t keyhelp)"
printf '  %s %s%s%s\n\n' "$(t keyget)" "$AZ" "$AYUDA_URL" "$R"
while :; do
    printf '  %s' "$(t keypaste)"
    read -rs CLAVE || { printf '\n'; exit 1; }
    printf '\n'
    case "$CLAVE" in
        "$PREFIJO"*) [ ${#CLAVE} -ge 20 ] && break ;;
    esac
    malo "$(t keybad)"
done
bien "$(t keyok) (${PREFIJO}...${#CLAVE} $( [ "$IDIOMA" = es ] && echo caracteres || echo chars))"

# --- 7. compilar -----------------------------------------------------------
titulo "$(t building)"
trap 'rm -f "$SECRETS"' EXIT   # la clave no sobrevive a este script

[ "$PROVEEDOR" = openai ] && ESOPENAI=1 || ESOPENAI=0
{
    echo "/* Generado por build.sh. NO se sube a git: lleva la API key. */"
    echo "#ifndef SECRETS_H"
    echo "#define SECRETS_H"
    echo "#define CFG_OPENAI $ESOPENAI"
    echo "#define CFG_API_KEY \"$CLAVE\""
    echo "#define CFG_MODEL \"$MODELO\""
    [ -n "$RAZONA" ] && echo "#define CFG_REASONING \"$RAZONA\""
    echo "#endif"
} > "$SECRETS"
unset CLAVE

# zcc parte las rutas por los espacios, y este proyecto puede vivir en una
# carpeta que los tenga. Compilamos a traves de un enlace sin ellos, y SIEMPRE
# fuera del repositorio: los directorios de CMake dentro del arbol acaban
# colandose en git.
SRC="$RAIZ/spectrum"
case "$RAIZ" in
  *\ *) LINK="$HOME/.zxchat-src"; ln -sfn "$RAIZ/spectrum" "$LINK"; SRC="$LINK";;
esac
OBJ="${TMPDIR:-/tmp}/zxchat-build"

barra 1 4 "cmake"
cmake -S "$SRC" -B "$OBJ" -DZXCHAT_LANG="$LANG_TAP" \
      -DCMAKE_TOOLCHAIN_FILE="$SPECTRANEXT_SDK_PATH/z88dk/support/cmake/Toolchain-zcc.cmake" \
      >/dev/null 2>&1 || { printf '\n'; malo "cmake"; exit 1; }
barra 2 4 "zcc"
if ! cmake --build "$OBJ" --target zxchat >"${TMPDIR:-/tmp}/zxchat-cc.log" 2>&1; then
    printf '\n'; malo "compilation failed:"; tail -15 "${TMPDIR:-/tmp}/zxchat-cc.log"; exit 1
fi
barra 3 4 "$(t built)"
cp "$OBJ/zxchat.tap" "$RAIZ/zxchat.tap"
rm -f "$SECRETS"
barra 4 4 "ok"

printf '\n'
bien "$(t built) ${N}zxchat.tap${R}  ($(wc -c < "$RAIZ/zxchat.tap" | tr -d ' ') bytes)"

# --- 8. subir al cartucho --------------------------------------------------
# Solo sale el epigrafe si hay algo que subir: preguntar por un cartucho que
# no esta seria ruido.
SUBIDO=0
PUERTO=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if command -v spx >/dev/null 2>&1 && [ -n "$PUERTO" ]; then
    titulo "$(t uploading)"
    printf '  %s' "$(t upload)"
    read -r S
    case "${S:-s}" in
        [SsYy]*)
            barra 1 2 "spx put"
            if spx --port "$PUERTO" --no-progress put "$RAIZ/zxchat.tap" zxchat >/dev/null 2>&1; then
                barra 2 2 "ok"
                bien "$(t sent) ${N}zxchat${R}"
                SUBIDO=1
            else
                printf '\n'; malo "spx put"
            fi;;
    esac
fi

# --- 9. siguientes pasos ---------------------------------------------------
# El nombre que hay que teclear depende de si llego a subirse: en el cartucho
# esta sin extension, y en una cinta que copies tu la llevara.
titulo "$(t next)"
if [ "$SUBIDO" = 1 ]; then
    NOMBRE="zxchat"
else
    NOMBRE="zxchat.tap"
    aviso "$(t nospx)"
    printf '  %s\n  %s\n\n' "$(t copyit)" "$(t copyit2)"
fi

printf '  %s\n\n' "$(t load)"
printf '    %s%%tapein "%s"%s\n' "$N" "$NOMBRE" "$R"
printf '    %sLOAD ""%s\n\n' "$N" "$R"

printf '  %s\n  %s\n\n' "$(t hide10)" "$(t hide11)"
printf '    %sCLEAR VAL "32767": %%tapein "%s": LOAD ""CODE : RANDOMIZE USR VAL "32768"%s\n\n' \
       "$D" "$NOMBRE" "$R"
