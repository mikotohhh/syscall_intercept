import os
import sys


path = sys.argv[1]
#we shall store all the file names in this list
filelist = []


for root, dirs, files in os.walk(path):
	for file in files:
        #append the file name to the list
		filelist.append(os.path.join(root,file))

#print all the file names
for name in filelist:
    # new_name = name.replace("fault_op_num-", "")
    # os.rename(name, new_name)
    print(name)
