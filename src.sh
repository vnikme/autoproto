#!/bin/bash
git ls-tree -r HEAD --name-only benchmark examples memprof mtproto td tdactor tdnet tdtl tdutils test | grep -E "\.cpp$|\.h$|\.hpp$"
