kill -9 $(pidof redis-server)

# proxy 1
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10010
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10011

##################################################################################
# proxy 1
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10000
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10001
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10002
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10003
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10004
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10005
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10006
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10007
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10008
./3rd_party/redis/bin/redis-server --daemonize yes --bind 0.0.0.0 --port 10009
 