# ZXChat - asistente de compilacion (Windows).
# Equivalente de build.sh. La clave NO se guarda: se escribe en secrets.h para
# compilar y se borra al terminar, pase lo que pase (finally).
$ErrorActionPreference = 'Stop'
$Raiz    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Secrets = Join-Path $Raiz 'spectrum\src\secrets.h'

function Logo {
    # Los mismos colores que pinta el Spectrum: ZX en blanco y CHAT en rojo,
    # amarillo, verde y cian.
    $letras = @(
      @('███████','     ██','   ███ ','  ██   ','███████'),
      @('██   ██',' ██ ██ ','  ███  ',' ██ ██ ','██   ██'),
      @(' ██████','██     ','██     ','██     ',' ██████'),
      @('██   ██','██   ██','███████','██   ██','██   ██'),
      @(' █████ ','██   ██','███████','██   ██','██   ██'),
      @('███████','   ██  ','   ██  ','   ██  ','   ██  '))
    $colores = @('White','White','Red','Yellow','Green','Cyan')
    ''
    for ($f = 0; $f -lt 5; $f++) {
        Write-Host '  ' -NoNewline
        for ($i = 0; $i -lt 6; $i++) {
            Write-Host $letras[$i][$f] -ForegroundColor $colores[$i] -NoNewline
            if ($i -lt 5) { Write-Host ' ' -NoNewline }
        }
        ''
    }
    Write-Host '        an AI chatbot that runs on a ZX Spectrum' -ForegroundColor DarkGray
    ''
}
function Titulo($t) { ''; Write-Host $t -ForegroundColor White; Write-Host ('-'*$t.Length) -ForegroundColor DarkGray }
function Bien($t)   { Write-Host '  v ' -ForegroundColor Green -NoNewline; Write-Host $t }
function Aviso($t)  { Write-Host '  ! ' -ForegroundColor Yellow -NoNewline; Write-Host $t }
function Malo($t)   { Write-Host '  x ' -ForegroundColor Red -NoNewline; Write-Host $t }
function Barra($h,$t,$txt) {
    $ancho = 32; $lleno = [int]($h*$ancho/$t)
    Write-Host ("`r  " + ('#'*$lleno) + ('.'*($ancho-$lleno)) + (' {0,4:0}%  {1,-24}' -f ($h*100/$t), $txt)) -NoNewline
    if ($h -eq $t) { '' }
}
function Menu($tit, $ops) {
    Titulo $tit
    for ($i=0; $i -lt $ops.Count; $i++) { Write-Host ('  {0}  {1}' -f ($i+1), $ops[$i]) }
    ''
    do { $r = Read-Host ('  Choose [1-{0}]' -f $ops.Count) } until ($r -match '^\d+$' -and [int]$r -ge 1 -and [int]$r -le $ops.Count)
    [int]$r
}
function Nota($v) { ('#'*$v) + ('.'*(5-$v)) }

# --- SDK -------------------------------------------------------------------
$Sdk = $env:SPECTRANEXT_SDK_PATH
if (-not $Sdk -or -not (Test-Path $Sdk)) {
    Logo
    Malo 'Spectranext SDK not found.'
    Write-Host "`n    Install it and set SPECTRANEXT_SDK_PATH to its folder.`n"
    exit 1
}

Logo
$es = (Menu 'ZXChat language on the Spectrum' @('English','Español')) -eq 2
$Lang = if ($es) { 'es' } else { 'en' }

$null = Menu (if($es){'Modelo de Spectrum'}else{'Spectrum model'}) `
             @('ZX Spectrum 48K / 128K', ('+2A / +3  (' + $(if($es){'próximamente'}else{'coming soon'}) + ')'))

# Anthropic no se puede elegir todavia: el codigo esta escrito pero nunca se ha
# ejecutado contra una clave real, y ofrecer un camino sin probar como si
# funcionara es peor que no ofrecerlo. El codigo sigue ahi, listo para el dia
# que se pruebe; lo unico cerrado es esta puerta.
do {
    $prov = Menu (if($es){'Proveedor de IA'}else{'AI provider'}) `
                 @('OpenAI', ('Anthropic (Claude)  (' + $(if($es){'próximamente'}else{'coming soon'}) + ')'))
    if ($prov -ne 1) {
        ''
        Aviso $(if($es){'Todavía no. La rama de Anthropic está escrita, pero nunca se ha ejecutado contra una clave real.'}else{'Not yet. The Anthropic path is written, but it has never been run against a real key.'})
        ''
    }
} until ($prov -eq 1)

if ($prov -eq 1) {
    $Proveedor = 'openai'; $Ayuda = 'https://platform.openai.com/api-keys'; $Prefijo = 'sk-'
    $Modelos = @(@('gpt-5.4',5,5,3),@('gpt-5.6-sol',5,5,5),@('gpt-5.4-mini',4,5,2),
                 @('gpt-5.6-terra',4,5,4),@('gpt-5.6-luna',3,4,2),@('gpt-5.5',3,2,5),
                 @('gpt-5.4-nano',2,5,1))
} else {
    $Proveedor = 'anthropic'; $Ayuda = 'https://console.anthropic.com/settings/keys'; $Prefijo = 'sk-ant-'
    $Modelos = @(@('claude-opus-5',5,2,5),@('claude-sonnet-5',4,4,3),@('claude-haiku-4-5',3,5,1))
}

Titulo (if($es){'Modelo'}else{'Model'})
Write-Host ('      {0,-20} {1,-8} {2,-8} {3,-8}' -f 'id',
    $(if($es){'calidad'}else{'quality'}), $(if($es){'veloc.'}else{'speed'}), $(if($es){'coste'}else{'cost'}))
for ($i=0; $i -lt $Modelos.Count; $i++) {
    Write-Host ('  {0}   {1,-20} {2,-8} {3,-8} {4,-8}' -f ($i+1), $Modelos[$i][0],
        (Nota $Modelos[$i][1]), (Nota $Modelos[$i][2]), (Nota $Modelos[$i][3]))
}
''
do { $r = Read-Host ('  Choose [1-{0}]' -f $Modelos.Count) } until ($r -match '^\d+$' -and [int]$r -ge 1 -and [int]$r -le $Modelos.Count)
$Modelo = $Modelos[[int]$r-1][0]

# Razonamiento, solo para OpenAI: Anthropic lo pide con otro campo.
$Razona = ''
if ($Proveedor -eq 'openai') {
    Write-Host ('  ' + $(if($es){'Cada nivel son segundos más de espera, y más tokens gastados: medio y alto pueden multiplicar por diez el coste.'}else{'Each level is seconds more waiting and more tokens spent: medium and high can multiply the cost by ten.'})) -ForegroundColor DarkGray
    $r = Menu (if($es){'Esfuerzo de razonamiento'}else{'Reasoning effort'}) @(
        $(if($es){'Ninguno   - responde al momento'}else{'None      - answers straight away'}),
        $(if($es){'Bajo      - piensa un poco antes de contestar'}else{'Low       - thinks a little first'}),
        $(if($es){'Medio     - más acierto, más espera'}else{'Medium    - more accurate, more waiting'}),
        $(if($es){'Alto      - lo mejor que sabe, y lo más lento'}else{'High      - its best, and its slowest'}))
    $Razona = @('','low','medium','high')[$r-1]
}

Titulo (if($es){'Clave de API'}else{'API key'})
Write-Host ('  ' + $(if($es){'Se usa solo para compilar y no se guarda en ningún sitio.'}else{'Used only to build, never stored anywhere.'}))
Write-Host ('  ' + $(if($es){'Consíguela en:'}else{'Get one at:'}) + ' ') -NoNewline
Write-Host $Ayuda -ForegroundColor Cyan
''
do {
    $seg = Read-Host ('  ' + $(if($es){'Pégala aquí'}else{'Paste it here'})) -AsSecureString
    $Clave = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
             [Runtime.InteropServices.Marshal]::SecureStringToBSTR($seg))
    if (-not ($Clave.StartsWith($Prefijo) -and $Clave.Length -ge 20)) {
        Malo (if($es){'Eso no parece una clave válida.'}else{'That does not look like a valid key.'})
        $Clave = $null
    }
} until ($Clave)
Bien (if($es){'Clave recibida'}else{'Key received'})

Titulo (if($es){'Compilando'}else{'Building'})
try {
    $esOpenAI = if ($Proveedor -eq 'openai') { 1 } else { 0 }
    @("/* Generado por build.ps1. NO se sube a git: lleva la API key. */",
      "#ifndef SECRETS_H", "#define SECRETS_H",
      "#define CFG_OPENAI $esOpenAI",
      "#define CFG_API_KEY `"$Clave`"",
      "#define CFG_MODEL `"$Modelo`"") +
      $(if ($Razona) { ,"#define CFG_REASONING `"$Razona`"" } else { @() }) +
      @("#endif") | Set-Content -Path $Secrets -Encoding ASCII
    $Clave = $null

    $src = Join-Path $Raiz 'spectrum'
    # Fuera del repositorio: los directorios de CMake dentro del arbol acaban
    # colandose en git.
    $obj = Join-Path $env:TEMP 'zxchat-build'
    Barra 1 4 'cmake'
    & cmake -S $src -B $obj "-DZXCHAT_LANG=$Lang" `
        "-DCMAKE_TOOLCHAIN_FILE=$Sdk/z88dk/support/cmake/Toolchain-zcc.cmake" *> $null
    Barra 2 4 'zcc'
    & cmake --build $obj --target zxchat *> $null
    if ($LASTEXITCODE -ne 0) { ''; Malo 'compilation failed'; exit 1 }
    Barra 3 4 'copy'
    Copy-Item (Join-Path $obj 'zxchat.tap') (Join-Path $Raiz 'zxchat.tap') -Force
    Barra 4 4 'ok'
} finally {
    Remove-Item $Secrets -ErrorAction SilentlyContinue
}

''
Bien ((if($es){'Listo:'}else{'Done:'}) + ' zxchat.tap')

# --- subir al cartucho -----------------------------------------------------
# El epigrafe solo sale si hay algo que subir.
$Subido = $false
if (Get-Command spx -ErrorAction SilentlyContinue) {
    Titulo (if($es){'Cargando en el cartucho'}else{'Uploading to the cartridge'})
    $s = Read-Host ('  ' + $(if($es){'¿Subirlo al Spectranext? [S/n]'}else{'Upload it to the Spectranext? [Y/n]'}))
    if ($s -eq '' -or $s -match '^[SsYy]') {
        & spx --no-progress put (Join-Path $Raiz 'zxchat.tap') zxchat
        if ($LASTEXITCODE -eq 0) {
            Bien (if($es){'Enviado al cartucho como zxchat'}else{'Sent to the cartridge as zxchat'})
            $Subido = $true
        } else { Malo 'spx put' }
    }
}

# --- siguientes pasos ------------------------------------------------------
# El nombre depende de si llego a subirse: en el cartucho esta sin extension.
Titulo (if($es){'Siguientes pasos'}else{"What's next"})
if ($Subido) {
    $Nombre = 'zxchat'
} else {
    $Nombre = 'zxchat.tap'
    Aviso (if($es){'No veo ningún Spectranext conectado.'}else{'No Spectranext connected.'})
    Write-Host ('  ' + $(if($es){'Copia zxchat.tap al cartucho, con spx o desde'}else{'Copy zxchat.tap to the cartridge, with spx or from'}))
    Write-Host ('  ' + $(if($es){'device.spectranext.net, y luego:'}else{'device.spectranext.net, and then:'}))
}
''
Write-Host ('  ' + $(if($es){'Cárgalo desde BASIC con:'}else{'Load it from BASIC with:'}))
''
Write-Host ("    %tapein `"$Nombre`"") -ForegroundColor White
Write-Host '    LOAD ""' -ForegroundColor White
''
Write-Host ('  ' + $(if($es){'Y si prefieres que no te deje la línea 10 en el'}else{'And if you would rather not have line 10 left in'}))
Write-Host ('  ' + $(if($es){'programa, cárgalo en una sola línea, en directo:'}else{'your program, load it in one line, in direct mode:'}))
''
Write-Host ("    CLEAR VAL `"32767`": %tapein `"$Nombre`": LOAD `"`"CODE : RANDOMIZE USR VAL `"32768`"") -ForegroundColor DarkGray
''
