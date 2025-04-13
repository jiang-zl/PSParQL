# 新建文件夹
hadoop fs -mkdir dir

# 上传文件
hadoop fs -put <local_files> ... <hdfs_path>

# 查看文件大小
hadoop fs -du -h 1.txt

# 下载文件
hadoop fs -get <hdfs_paths> <local_path>

# 删除文件
hadoop fs -rm <path>


