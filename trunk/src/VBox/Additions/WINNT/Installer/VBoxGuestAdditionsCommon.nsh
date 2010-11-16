
!ifndef UNINSTALLER_ONLY
Function ExtractFiles

  ; @todo: Use a define for all the file specs to group the files per module
  ; and keep the redundancy low

  Push $0
  StrCpy "$0" "$INSTDIR\$%BUILD_TARGET_ARCH%"

  ; Video driver
  SetOutPath "$0\VBoxVideo"
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.inf"
!ifdef VBOX_SIGN_ADDITIONS
  FILE "$%PATH_OUT%\bin\additions\VBoxVideo.cat"
!endif
  FILE "$%PATH_OUT%\bin\additions\VBoxDisp.dll"

!if $%VBOX_WITH_CROGL% == "1"
  ; crOpenGL
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLarrayspu.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLcrutil.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLerrorspu.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLpackspu.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLpassthroughspu.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGLfeedbackspu.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxOGL.dll"

  ; Do *not* install 64-bit d3d files - they don't work yet
  !if $%BUILD_TARGET_ARCH% == "x86"
    SetOutPath "$0\VBoxVideo\OpenGL"
    FILE "$%PATH_OUT%\bin\additions\d3d8.dll"
    FILE "$%PATH_OUT%\bin\additions\d3d9.dll"
    FILE "$%PATH_OUT%\bin\additions\libWine.dll"
    FILE "$%PATH_OUT%\bin\additions\VBoxD3D8.dll"
    FILE "$%PATH_OUT%\bin\additions\VBoxD3D9.dll"
    FILE "$%PATH_OUT%\bin\additions\wined3d.dll"
  !endif

  !if $%BUILD_TARGET_ARCH% == "amd64"
    ; Only 64-bit installer: Also copy 32-bit DLLs on 64-bit target
    SetOutPath "$0\VBoxVideo\OpenGL\SysWow64"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\d3d8.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\d3d9.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\libWine.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLarrayspu.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLcrutil.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLerrorspu.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLpackspu.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLpassthroughspu.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGLfeedbackspu.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxOGL.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxD3D8.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\VBoxD3D9.dll"
    FILE "$%VBOX_PATH_ADDITIONS_WIN_X86%\wined3d.dll"
  !endif
!endif

  ; Mouse driver
  SetOutPath "$0\VBoxMouse"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.inf"
!ifdef VBOX_SIGN_ADDITIONS
  FILE "$%PATH_OUT%\bin\additions\VBoxMouse.cat"
!endif

!if $%BUILD_TARGET_ARCH% == "x86"
  SetOutPath "$0\VBoxMouse\NT4"
  FILE "$%PATH_OUT%\bin\additions\VBoxMouseNT.sys"
!endif

  ; Guest driver
  SetOutPath "$0\VBoxGuest"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.inf"
!ifdef VBOX_SIGN_ADDITIONS
  FILE "$%PATH_OUT%\bin\additions\VBoxGuest.cat"
!endif
  FILE "$%PATH_OUT%\bin\additions\VBCoInst.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxTray.exe"
  FILE "$%PATH_OUT%\bin\additions\VBoxHook.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxControl.exe"

!if $%BUILD_TARGET_ARCH% == "x86"
  SetOutPath "$0\VBoxGuest\NT4"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuestNT.sys"
!endif

  ; VBoxService
  SetOutPath "$0\Bin"
  FILE "$%PATH_OUT%\bin\additions\VBoxService.exe"
!if $%BUILD_TARGET_ARCH% == "x86"
  FILE "$%PATH_OUT%\bin\additions\VBoxServiceNT.exe"
!endif

  ; Shared Folders
  SetOutPath "$0\VBoxSF"
  FILE "$%PATH_OUT%\bin\additions\VBoxSF.sys"
  FILE "$%PATH_OUT%\bin\additions\VBoxMRXNP.dll"

  ; Auto-Logon
  SetOutPath "$0\AutoLogon"
  FILE "$%PATH_OUT%\bin\additions\VBoxGINA.dll"
  FILE "$%PATH_OUT%\bin\additions\VBoxCredProv.dll"

  ; Misc tools
  SetOutPath "$0\Tools"
  FILE "$%PATH_OUT%\bin\additions\VBoxDrvInst.exe"

!if $%BUILD_TARGET_ARCH% == "x86"
  SetOutPath "$0\Tools\NT4"
  FILE "$%PATH_OUT%\bin\additions\VBoxGuestDrvInst.exe"
  FILE "$%PATH_OUT%\bin\additions\RegCleanup.exe"
!endif

  Pop $0

FunctionEnd
!endif ; UNINSTALLER_ONLY

!macro EnableLog un
Function ${un}EnableLog

!ifdef _DEBUG
  Goto log
!endif

  StrCmp $g_bLogEnable "true" log
  Goto exit

log:

  LogSet on
  LogText "Start logging."

exit:

FunctionEnd
!macroend
!insertmacro EnableLog ""
!insertmacro EnableLog "un."

!macro WriteLogUI un
Function ${un}WriteLogUI

  IfSilent exit

!ifdef _DEBUG
  Goto log
!endif

  StrCmp $g_bLogEnable "true" log
  Goto exit

log:

  ; Dump log to see what happened
  StrCpy $0 "$INSTDIR\${un}install_ui.log"
  Push $0
  Call ${un}DumpLog

exit:

FunctionEnd
!macroend
!insertmacro WriteLogUI ""
!insertmacro WriteLogUI "un."

!macro WriteLogVBoxTray un
Function ${un}WriteLogVBoxTray

  ; Pop function parameters off the stack
  ; in reverse order
  Exch $1 ; Message type (0=Info, 1=Warning, 2=Error)
  Exch
  Exch $0 ; Body string

  ; @todo Add more paramters here!
!if $%VBOX_WITH_GUEST_INSTALL_HELPER% == "1"
  ${If} $g_bPostInstallStatus == "true"
    ; Parameters:
    ; - String: Description / Body
    ; - String: Title / Name of application
    ; - Integer: Type of message: 0 (Info), 1 (Warning), 2 (Error)
    ; - Integer: Time (in msec) to show the notification
    VBoxGuestInstallHelper::VBoxTrayShowBallonMsg "$0" "VirtualBox Guest Additions Setup" $1 5000
    Pop $0 ; Get return value (ignored for now)
  ${EndIf}
!endif
  Pop $0
  Pop $1

FunctionEnd
!macroend
!insertmacro WriteLogVBoxTray ""
!insertmacro WriteLogVBoxTray "un."

!macro GetWindowsVer un
Function ${un}GetWindowsVer

  ; Check if we are running on w2k or above
  ; For other windows versions (>XP) it may be necessary to change winver.nsh
  Call ${un}GetWindowsVersion
  Pop $R3     ; Windows Version

  Push $R3    ; The windows version string
  Push "NT"   ; String to search for. Win 2k family returns no string containing 'NT'
  Call ${un}StrStr
  Pop $R0
  StrCmp $R0 '' nt5plus       ; Not NT 3.XX or 4.XX

  ; Ok we know it is NT. Must be a string like NT X.XX
  Push $R3    ; The windows version string
  Push "4."   ; String to search for
  Call ${un}StrStr
  Pop $R0
  StrCmp $R0 "" nt5plus nt4   ; If empty -> not NT 4

nt5plus:    ; Windows 2000+ (XP, Vista, ...)

  StrCpy $g_strWinVersion $R3
  goto exit

nt4:        ; NT 4.0

  StrCpy $g_strWinVersion "NT4"
  goto exit

exit:

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro GetWindowsVer ""
!insertmacro GetWindowsVer "un."

!macro GetAdditionsVersion un
Function ${un}GetAdditionsVersion

  Push $0
  Push $1

  ; Get additions version
  ReadRegStr $0 HKLM "SOFTWARE\$%VBOX_VENDOR_SHORT%\VirtualBox Guest Additions" "Version"

  ; Get revision
  ReadRegStr $g_strAddVerRev HKLM "SOFTWARE\$%VBOX_VENDOR_SHORT%\VirtualBox Guest Additions" "Revision"

  ; Extract major version
  Push "$0"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerMaj

  ; Extract minor version
  Push "$0"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push ">"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $1          ; Got first part (e.g. "1.5")

  Push "$1"       ; String
  Push "."        ; SubString
  Push ">"        ; SearchDirection
  Push "<"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerMin   ; Extracted second part (e.g. "5" from "1.5")

  ; Extract build number
  Push "$0"       ; String
  Push "."        ; SubString
  Push "<"        ; SearchDirection
  Push ">"        ; StrInclusionDirection
  Push "0"        ; IncludeSubString
  Push "0"        ; Loops
  Push "0"        ; CaseSensitive
  Call ${un}StrStrAdv
  Pop $g_strAddVerBuild

exit:

  Pop $1
  Pop $0

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro GetAdditionsVersion ""
!insertmacro GetAdditionsVersion "un."

!macro IsUserAdmin un
Function ${un}IsUserAdmin

  ; Check if current user has admin rights
  UserInfo::GetAccountType
  Pop $0
  StrCmp $0 "Admin" is_admin 0
    MessageBox MB_ICONSTOP $(VBOX_NOADMIN)
    Abort

is_admin:

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro IsUserAdmin ""
!insertmacro IsUserAdmin "un."

!macro StopVBoxService un
Function ${un}StopVBoxService

  Push $0   ; Temp results
  Push $1
  Push $2   ; Image name of VBoxService
  Push $3   ; Safety counter

  StrCpy $3 "0" ; Init counter
  DetailPrint "Stopping VBoxService ..."

svc_stop:

  LogText "Stopping VBoxService (as service) ..."
  ${If} $g_strWinVersion == "NT4"
   nsExec::Exec '"$SYSDIR\net.exe" stop VBoxService'
  ${Else}
   nsExec::Exec '"$SYSDIR\SC.exe" stop VBoxService'
  ${EndIf}
  Sleep "1000"           ; Wait a bit

exe_stop:

!ifdef _DEBUG
  DetailPrint "Stopping VBoxService (as exe) ..."
!endif

exe_stop_loop:

  IntCmp $3 10 exit      ; Only try this loop 10 times max
  IntOp  $3 $3 + 1       ; Increment

  LogText "Try: $3"

  ${If} $g_strWinVersion == "NT4"
    StrCpy $2 "VBoxServiceNT.exe"
  ${Else}
    StrCpy $2 "VBoxService.exe"
  ${EndIf}

  ${nsProcess::FindProcess} $2 $0
  StrCmp $0 0 0 exit

  ${nsProcess::KillProcess} $2 $0
  Sleep "1000"           ; Wait a bit
  Goto exe_stop_loop

exit:

  DetailPrint "Stopping VBoxService done."

  Pop $3
  Pop $2
  Pop $1
  Pop $0

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro StopVBoxService ""
!insertmacro StopVBoxService "un."

!macro StopVBoxTray un
Function ${un}StopVBoxTray

  Push $0   ; Temp results
  Push $1   ; Safety counter

  StrCpy $1 "0" ; Init counter
  DetailPrint "Stopping VBoxTray ..."

exe_stop:

  IntCmp $1 10 exit      ; Only try this loop 10 times max
  IntOp  $1 $1 + 1       ; Increment

  ${nsProcess::FindProcess} "VBoxTray.exe" $0
  StrCmp $0 0 0 exit

  ${nsProcess::KillProcess} "VBoxTray.exe" $0
  Sleep "1000"           ; Wait a bit
  Goto exe_stop

exit:

  DetailPrint "Stopping VBoxTray done."

  Pop $1
  Pop $0

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro StopVBoxTray ""
!insertmacro StopVBoxTray "un."

!macro WriteRegBinR ROOT KEY NAME VALUE
  WriteRegBin "${ROOT}" "${KEY}" "${NAME}" "${VALUE}"
!macroend

!macro AbortShutdown un
Function ${un}AbortShutdown

  Push $0

  ; Try to abort the shutdown
  nsExec::ExecToLog '"$g_strSystemDir\shutdown.exe" -a' $0

  Pop $0

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro AbortShutdown ""
!insertmacro AbortShutdown "un."

!macro CheckForWDDMCapability un
Function ${un}CheckForWDDMCapability

  ; Note: This is done early at startup of the installer, so
  ;       DetailPrint and friends won't work here!

!if $%BUILD_TARGET_ARCH% == "x86"
  ; If we're on a 32-bit Windows Vista / 7 we can use the WDDM driver
  ${If} $g_strWinVersion == "Vista"
  ${OrIf} $g_strWinVersion == "7"
    StrCpy $g_bCapWDDM "true"
  ${EndIf}
!endif

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro CheckForWDDMCapability ""
!insertmacro CheckForWDDMCapability "un."

!macro CheckForCapabilities un
Function ${un}CheckForCapabilities

    Call ${un}CheckForWDDMCapability

FunctionEnd
!macroend

; Insert function as an installer and uninstaller function
!insertmacro CheckForCapabilities ""
!insertmacro CheckForCapabilities "un."

