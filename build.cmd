@echo off
REM ZXChat - asistente de compilacion para Windows.
REM El trabajo lo hace build.ps1: batch no da para colores ni entrada oculta.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
