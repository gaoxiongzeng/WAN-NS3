import sys
import os


if __name__ == "__main__":
  #for protocol in ["ns3::TcpBbr"]: # for test
  for protocol in ["ns3::TcpBbr", "ns3::TcpCubic"]:
    #for bSize in [1000]: # for test
    for bSize in [66666, 33333, 13333, 6666, 3333, 1333]: # in packets (50MB=33.3kMTU)
      os.system("./waf --run 'scratch/static-flow --protocol=%s --bSize=%s' > %s-buf%s-printlog 2>&1 &" % (protocol, bSize, protocol, bSize))
