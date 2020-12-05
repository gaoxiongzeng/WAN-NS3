import sys
import os

def parse(file):
  f = open(file)
  lines = f.readlines()
  f.close()

  count_line = 0
  init_loss = -1
  end_loss = -1
  throughput_sum = 0.0
  queue_sum = 0
  ignored_line = 500 # ignore slow start phase
  for each in lines:
    count_line += 1
    if count_line < ignored_line: 
      continue
    words = each.split(' ')
    if len(words) == 9:
      if init_loss == -1:
        init_loss = int(words[8])
      else:
        end_loss = int(words[8])
      throughput_sum += float(words[3])
      queue_sum += int(words[5])

  avg_throughput = throughput_sum/(count_line-ignored_line)
  avg_queue = queue_sum/(count_line-ignored_line)
  loss_rate = (end_loss-init_loss)/((end_loss-init_loss)+throughput_sum*1000/8/1500)
  print(avg_throughput, avg_queue, loss_rate)


if __name__ == "__main__":
  #for protocol in ["ns3::TcpBbr"]: # for test
  for protocol in ["ns3::TcpBbr", "ns3::TcpCubic"]:
    #for bSize in [3333]: # for test
    for bSize in [66666, 33333, 13333, 6666, 3333, 1333]: # in packets (50MB=33.3kMTU)
       file = str(protocol) + "-buf" + str(bSize) + "-printlog"
       parse(file)