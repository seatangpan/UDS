function Component()
{
}

Component.prototype.createOperations = function ()
{
    component.createOperations()
    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/argProcessName.exe",
                               "@StartMenuDir@/argLinkName.lnk", "workingDirectory=@TargetDir@",
                               "iconPath=@TargetDir@/argProcessName.exe", "description=UDS")
        component.addOperation("CreateShortcut", "@TargetDir@/argProcessName.exe",
                               "@DesktopDir@/argLinkName.lnk", "workingDirectory=@TargetDir@",
                               "iconPath=@TargetDir@/argProcessName.exe", "description=UDS")
        component.addOperation("CreateShortcut", "@TargetDir@/maintenancetool.exe",
                               "@StartMenuDir@/Uninstall.lnk", "workingDirectory=@TargetDir@",
                               "iconPath=%SystemRoot%/system32/SHELL32.dll", "iconId=101",
                               "description=Uninstall UDS", "--start-uninstaller")
    }
}
