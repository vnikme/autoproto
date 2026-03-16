git ls-tree -r HEAD --name-only benchmark examples memprof mtproto td tdactor tdnet tdtl tdutils test | Select-String "\.cpp$|\.h$|\.hpp$"
