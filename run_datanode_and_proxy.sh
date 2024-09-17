kill -9 $(pidof run_datanode)
kill -9 $(pidof run_proxy)

# datanodes in proxy 
./build/run_datanode 0.0.0.0 9000
./build/run_datanode 0.0.0.0 9001
./build/run_datanode 0.0.0.0 9002
./build/run_datanode 0.0.0.0 9003
./build/run_datanode 0.0.0.0 9004
./build/run_datanode 0.0.0.0 9005
./build/run_datanode 0.0.0.0 9006
./build/run_datanode 0.0.0.0 9007
./build/run_datanode 0.0.0.0 9008
./build/run_datanode 0.0.0.0 9009

# cachenodes in proxy 1
./build/run_datanode 0.0.0.0 9010
./build/run_datanode 0.0.0.0 9011


# run proxy
./build/run_proxy 0.0.0.0 50005 > output.log
