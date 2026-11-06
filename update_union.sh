#########################################################################
# File Name: update_union.sh
# Desc:
# Author: Andy-wei.hou
# mail: wei.hou@scaleflux.com
# Created Time: 2026年11月06日 星期五 15时53分49秒
# Log: 
#########################################################################
#!/bin/bash
sudo ./build/tools/mbcci-sfx/mbcci-sfx mem0 vu-dlunion --input $1 --image union --partition A
sudo ./build/tools/mbcci-sfx/mbcci-sfx mem0 vu-dlunion --input $1 --image union --partition B
sudo ./build/tools/mbcci-sfx/mbcci-sfx mem0 vu-dlpt --input ../partition_table.bin 
