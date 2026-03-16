./src.ps1 | Select-String -NotMatch "/tl-parser/" | ForEach-Object {
  clang-format -verbose -style=file -i $_
}
