!include "MUI2.nsh"

!define VERSION "0.1"
Name "Kos programming language ${VERSION}"
OutFile "Kos-${VERSION}.exe"
Unicode True

InstallDir "${PROGRAMFILES}\Kos"
InstallDirRegKey HKCU "Software\Kos" ""
RequestExecutionLevel user

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
  
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

!define Ext ".kos"
!define ExtDesc "Kos script"

Section "InstallSection" SecInstall

    ; Install executable
    SetOutPath "${INSTDIR}"
    File "kos.exe"

    ; Install modules
    CreateDirectory "${INSTDIR}\modules"
    SetOutPath "${INSTDIR}\modules"
    File "modules\*.*"

    ; Save installation path
    WriteRegStr HKCU "Software\Kos" "" ${INSTDIR}

    ; Install extension association
    WriteRegStr HKCR "${Ext}" "" "${ExtDesc}"
    WriteRegStr HKCR "${ExtDesc}" "" "${ExtDesc}"
    WriteRegStr HKCR "${ExtDesc}\shell" "" "open"
    ;WriteRegStr HKCR "${ExtDesc}\DefaultIcon" "" "${INSTDIR}\Kos.exe,0"
    WriteRegStr HKCR "${ExtDesc}\shell\open\command" "" '"${INSTDIR}\Kos.exe" "%1"'

    ; Write uninstaller
    WriteUninstaller "${INSTDIR}\Uninstall.exe"
SectionEnd

LangString DESC_SecInstall ${LANG_ENGLISH} "Install section."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
!insertmacro MUI_DESCRIPTION_TEXT ${SecInstall} $(DESC_SecInstall)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"

    ; Remove file association
    DeleteRegKey HKCR "${Ext}"
    DeleteRegKey HKCR "${ExtDesc}"

    ; Remove installed files
    RMDir /r "${INSTDIR}"

    ; Remove Kos's own registry key
    DeleteRegKey /ifempty HKCU "Software\Kos"
SectionEnd
