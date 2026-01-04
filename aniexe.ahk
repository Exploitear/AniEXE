#NoEnv
SendMode Input
SetWorkingDir %A_ScriptDir%
#SingleInstance Force

; =========================
; CONFIG
; =========================
mpvPath := "C:\Program Files\mpv.net\mpvnet.exe"

; Fallback search for mpv executable
if !FileExist(mpvPath)
{
    commonPaths := [ "C:\Program Files\mpv\mpv.exe"
                   , "C:\Program Files (x86)\mpv\mpv.exe"
                   , "C:\Program Files\mpv.net\mpvnet.exe"
                   , "C:\Program Files (x86)\mpv.net\mpvnet.exe"
                   , A_AppData . "\mpv\mpv.exe" ]
    
    for _, path in commonPaths
    {
        if FileExist(path)
        {
            mpvPath := path
            break
        }
    }
    
    ; If still not found, rely on PATH
    if !FileExist(mpvPath)
        mpvPath := "mpvnet.exe"
}

; =========================
; COLLECT AND SORT VIDEO FILES
; =========================
videoExtensions := ["mkv", "mp4", "avi", "webm", "mov", "flv", "wmv", "m4v", "ts", "ogm", "m2ts"]
videos := []

for _, ext in videoExtensions
{
    Loop, Files, %A_ScriptDir%\*.%ext%, F
    {
        videos.Push(A_LoopFileFullPath)
    }
}

if (videos.Length() = 0)
{
    MsgBox, 48, Anime Launcher, No video files found in:`n`n%A_ScriptDir%`n`nSupported formats: MKV, MP4, AVI, WEBM, MOV, FLV, WMV, M4V, TS, OGM, M2TS
    ExitApp
}

; Simple alphabetical sort (works fine with padded numbers like "Episode 01.mkv")
Sort, videos, D`n

; Build file list argument
fileList := ""
for _, file in videos
    fileList .= " """ . file . """"

; =========================
; LAUNCH MPV WITH EXPLICIT PLAYLIST
; =========================
; Build mpv command with quality-of-life flags (FLAGS FIRST, FILES LAST)
mpvArgs := "--config-dir=""" . A_ScriptDir . "\mpv-config"""  ; Use local config instead of global
    . " --save-position-on-quit"           ; Resume from last position
    . " --force-window=yes"                ; Always show window
    . " --keep-open=yes"                   ; Keep window open after playlist ends
    . " --fs"                              ; Start in fullscreen
    . " --osd-level=1"                     ; Show playback info
    . " --osd-duration=2000"               ; OSD timeout (ms)
    . " --osd-font-size=32"                ; Larger OSD text
    . " --volume=80"                       ; Default volume (safer level)
    . " --volume-max=150"                  ; Allow volume boost
    . " --audio-normalize-downmix=yes"     ; Normalize audio levels
    . " --sub-auto=fuzzy"                  ; Auto-load subtitles
    . " --sub-file-paths=subs:subtitles"   ; Common subtitle folders
    . " --screenshot-directory=""" . A_ScriptDir . "\screenshots"""  ; Screenshot location (quoted for spaces)
    . " --screenshot-template=""%F_%p"""   ; Screenshot naming
    . " --hwdec=auto-safe"                 ; Hardware acceleration
    . " --profile=gpu-hq"                  ; High quality rendering
    . " --deband=yes"                      ; Reduce color banding
    . " --no-terminal"                     ; Hide console
    . fileList                             ; Individual video files (LAST)

; Create necessary folders if they don't exist
screenshotDir := A_ScriptDir . "\screenshots"
configDir := A_ScriptDir . "\mpv-config"
if !FileExist(screenshotDir)
    FileCreateDir, %screenshotDir%
if !FileExist(configDir)
    FileCreateDir, %configDir%

; Launch mpv and wait for it to exit (for Lutris playtime tracking)
RunWait, "%mpvPath%" %mpvArgs%, , UseErrorLevel

; Check for launch errors
if (ErrorLevel)
{
    MsgBox, 16, Anime Launcher - Error, Failed to launch mpv player!`n`nPath: %mpvPath%`nFolder: %A_ScriptDir%`n`nPlease verify mpv is installed correctly.
    ExitApp, 1
}

ExitApp