$path = 'OpenGL\OpenGL.vcxproj'
$txt = [IO.File]::ReadAllText($path)

# Match ItemDefinitionGroup for Release|x64 specifically
$pattern = '(<ItemDefinitionGroup Condition="''\$\(Configuration\)\|\$\(Platform\)''==''Release\|x64''">[\s\S]*?<ClCompile>)'
if ($txt -match $pattern) {
    # Check if LanguageStandard is INSIDE this specific block (not just anywhere after)
    # We grab the whole ItemDefinitionGroup block first
    $blockPattern = '<ItemDefinitionGroup Condition="''\$\(Configuration\)\|\$\(Platform\)''==''Release\|x64''">([\s\S]*?)</ItemDefinitionGroup>'
    if ($txt -match $blockPattern) {
        $blockContent = $matches[1]
        if ($blockContent -notmatch '<LanguageStandard>stdcpp17</LanguageStandard>') {
            # It's missing in this block, so insert it
            $replacement = '${1}' + "`r`n      <LanguageStandard>stdcpp17</LanguageStandard>"
            $txt = $txt -replace $pattern, $replacement
            [IO.File]::WriteAllText($path, $txt)
            Write-Host "Added C++17 standard to Release configuration"
        } else {
            Write-Host "Already present in ItemDefinitionGroup"
        }
    }
} else {
    Write-Host "Could not find Release|x64 ItemDefinitionGroup"
}
