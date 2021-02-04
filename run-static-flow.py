import sys
import os
import time

if __name__ == "__main__":
  #for protocol in ["ns3::TcpCubic"]: # for test
  for protocol in ["ns3::TcpBbr", "ns3::TcpCubic"]:
    #for bSize in [100000]: # for test
    for bSize in [333333, 133333, 66666, 33333, 13333, 6666, 3333, 1333, 666, 333, 133]: # in packets (50MB=33.3kMTU)
      os.system("./waf --run 'scratch/static-flow --protocol=%s --bSize=%s' > %s-buf%s-static-log 2>&1 &" % (protocol, bSize, protocol, bSize))
      time.sleep(0.2)
