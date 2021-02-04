from __future__ import division
import sys
import os
import glob
try:
    from xml.etree import cElementTree as ElementTree
except ImportError:
    from xml.etree import ElementTree


def parse_time_ns(tm):
    if tm.endswith('ns'):
        return long(tm[:-4])
    raise ValueError(tm)


class FiveTuple(object):
    __slots__ = ['sourceAddress', 'destinationAddress', 'protocol', 'sourcePort', 'destinationPort']
    def __init__(self, el):
        self.sourceAddress = el.get('sourceAddress')
        self.destinationAddress = el.get('destinationAddress')
        self.sourcePort = int(el.get('sourcePort'))
        self.destinationPort = int(el.get('destinationPort'))
        self.protocol = int(el.get('protocol'))


class Histogram(object):
    __slots__ = 'bins', 'nbins', 'number_of_flows'
    def __init__(self, el=None):
        self.bins = []
        if el is not None:
            #self.nbins = int(el.get('nBins'))
            for bin in el.findall('bin'):
                self.bins.append( (float(bin.get("start")), float(bin.get("width")), int(bin.get("count"))) )


class Flow(object):
    __slots__ = ['flowId', 'delayMean', 'packetLossRatio', 'rxBitrate', 'txBitrate',
                 'fiveTuple', 'packetSizeMean', 'probe_stats_unsorted',
                 'hopCount', 'flowInterruptionsHistogram', 'rx_duration',
                 'fct', 'txBytes', 'txPackets', 'rxPackets', 'rxBytes', 'lostPackets', 'throughput']
    def __init__(self, flow_el):
        self.flowId = int(flow_el.get('flowId'))
        rxPackets = long(flow_el.get('rxPackets'))
        txPackets = long(flow_el.get('txPackets'))
        tx_duration = float(long(flow_el.get('timeLastTxPacket')[:-4]) - long(flow_el.get('timeFirstTxPacket')[:-4]))*1e-9
        rx_duration = float(long(flow_el.get('timeLastRxPacket')[:-4]) - long(flow_el.get('timeFirstRxPacket')[:-4]))*1e-9
        fct = float(long(flow_el.get('timeLastRxPacket')[:-4]) - long(flow_el.get('timeFirstTxPacket')[:-4]))*1e-9
        txBytes = long(flow_el.get('txBytes'))
        rxBytes = long(flow_el.get('rxBytes'))
        self.txBytes = txBytes
        self.txPackets = txPackets
        self.rxBytes = rxBytes
        self.rxPackets = rxPackets
        self.rx_duration = rx_duration
        throughput = rxBytes * 8 / fct / 1024 / 1024
        if fct > 0:
            self.fct = fct
        else:
            self.fct = None
        if throughput > 0:
            self.throughput = throughput
        else:
            self.throughput = None
        self.probe_stats_unsorted = []
        if rxPackets:
            self.hopCount = float(flow_el.get('timesForwarded')) / rxPackets + 1
        else:
            self.hopCount = -1000
        if rxPackets:
            self.delayMean = float(flow_el.get('delaySum')[:-4]) / rxPackets * 1e-9
            self.packetSizeMean = float(flow_el.get('rxBytes')) / rxPackets
        else:
            self.delayMean = None
            self.packetSizeMean = None
        if rx_duration > 0:
            self.rxBitrate = long(flow_el.get('rxBytes'))*8 / rx_duration
        else:
            self.rxBitrate = None
        if tx_duration > 0:
            self.txBitrate = long(flow_el.get('txBytes'))*8 / tx_duration
        else:
            self.txBitrate = None
        lost = float(flow_el.get('lostPackets'))
        self.lostPackets = lost
        #print "rxBytes: %s; txPackets: %s; rxPackets: %s; lostPackets: %s" % (flow_el.get('rxBytes'), txPackets, rxPackets, lost)
        if rxPackets == 0:
            self.packetLossRatio = None
        else:
            self.packetLossRatio = (lost / (rxPackets + lost))

        interrupt_hist_elem = flow_el.find("flowInterruptionsHistogram")
        if interrupt_hist_elem is None:
            self.flowInterruptionsHistogram = None
        else:
            self.flowInterruptionsHistogram = Histogram(interrupt_hist_elem)


class ProbeFlowStats(object):
    __slots__ = ['probeId', 'packets', 'bytes', 'delayFromFirstProbe']


class Simulation(object):
    def __init__(self, simulation_el):
        self.flows = []
        FlowClassifier_el, = simulation_el.findall("Ipv4FlowClassifier")
        flow_map = {}
        for flow_el in simulation_el.findall("FlowStats/Flow"):
            flow = Flow(flow_el)
            flow_map[flow.flowId] = flow
            self.flows.append(flow)
        for flow_cls in FlowClassifier_el.findall("Flow"):
            flowId = int(flow_cls.get('flowId'))
            flow_map[flowId].fiveTuple = FiveTuple(flow_cls)

        for probe_elem in simulation_el.findall("FlowProbes/FlowProbe"):
            probeId = int(probe_elem.get('index'))
            for stats in probe_elem.findall("FlowStats"):
                flowId = int(stats.get('flowId'))
                s = ProbeFlowStats()
                s.packets = int(stats.get('packets'))
                s.bytes = long(stats.get('bytes'))
                s.probeId = probeId
                if s.packets > 0:
                    s.delayFromFirstProbe =  parse_time_ns(stats.get('delayFromFirstProbeSum')) / float(s.packets)
                else:
                    s.delayFromFirstProbe = 0
                flow_map[flowId].probe_stats_unsorted.append(s)


def parse (fileName):
    file_obj = open(fileName)
    print "Reading XML file",

    sys.stdout.flush()
    level = 0
    sim_list = []
    for event, elem in ElementTree.iterparse(file_obj, events=("start", "end")):
        if event == "start":
            level += 1
        if event == "end":
            level -= 1
            if level == 0 and elem.tag == 'FlowMonitor':
                sim = Simulation(elem)
                sim_list.append(sim)
                elem.clear() # won't need this any more
                sys.stdout.write(".")
                sys.stdout.flush()
    print " done."

    total_fct = 0
    flow_count = 0
    large_flow_total_fct = 0
    large_flow_count = 0
    middle_flow_total_fct = 0
    middle_flow_count = 0
    small_flow_total_fct = 0
    small_flow_count = 0

    total_lost_packets = 0
    total_packets = 0
    total_rx_packets = 0

    total_large_throughput = 0
    avg_large_throughput = 0

    max_small_flow_id = 0
    max_small_flow_fct = 0

    flow_list = []
    small_flow_list = []

    avg_fct = -1.0
    avg_small_fct = -1.0
    avg_middle_fct = -1.0
    avg_large_fct = -1.0

    for sim in sim_list:
        for flow in sim.flows:
            if flow.fct == None or flow.txBitrate == None or flow.rxBitrate == None or flow.throughput == None:
                continue
            # remove ACK flows
            if flow.txBytes <= 70 * flow.txPackets:
                continue
            flow_count += 1
            total_fct += flow.fct
            total_packets += flow.txPackets
            total_rx_packets += flow.rxPackets
            total_lost_packets += flow.lostPackets
            flow_list.append(flow)
            if flow.rxBytes > 10000000: # large flow size: >10MB
                large_flow_count += 1
                large_flow_total_fct += flow.fct
                total_large_throughput += flow.throughput
            elif flow.rxBytes > 100000: # small flow size: <100KB
                middle_flow_count += 1
                middle_flow_total_fct += flow.fct
            else:
                small_flow_count += 1
                small_flow_total_fct += flow.fct
                if flow.fct > max_small_flow_fct:
                    max_small_flow_id = flow.flowId
                    max_small_flow_fct = flow.fct
                small_flow_list.append(flow)
            t = flow.fiveTuple
            proto = {6: 'TCP', 17: 'UDP'} [t.protocol]
            print "FlowID: %i (%s %s/%s --> %s/%i)" % (flow.flowId, proto, t.sourceAddress, t.sourcePort, t.destinationAddress, t.destinationPort)
            print "\tTX bitrate: %.2f kbit/s" % (flow.txBitrate*1e-3,)
            print "\tRX bitrate: %.2f kbit/s" % (flow.rxBitrate*1e-3,)
            print "\tMean Delay: %.2f ms" % (flow.delayMean*1e3,)
            print "\tPacket Loss Ratio: %.2f %%" % (flow.packetLossRatio*100)
            print "\tFlow size: %i bytes, %i packets" % (flow.txBytes, flow.txPackets)
            print "\tRx %i bytes, %i packets" % (flow.rxBytes, flow.rxPackets)
            print "\tDevice Lost %i packets" % (flow.lostPackets)
            print "\tReal Lost %i packets" % (flow.txPackets - flow.rxPackets)
            print "\tFCT: %.4f" % (flow.fct)

    print "Total Flow #: %i" % flow_count
    print "Total TX Packets: %i" % total_packets
    print "Total RX Packets: %i" % total_rx_packets
    print "Total Lost Packets: %i" % total_lost_packets
    print "Total Lost Rate: %.5f" % (total_lost_packets / total_packets)

    avg_fct = (total_fct / flow_count)
    print "All flow FCT: %.4f" % avg_fct

    flow_list.sort (key=lambda x: x.fct)
    index_99 = int(len(flow_list) * 0.99)
    flow_fct_99 = flow_list[index_99].fct
    print "ALL flow 99 FCT: %.4f" % flow_fct_99
    
    if small_flow_count == 0:
        print "No small flows"
    else:
        avg_small_fct = (small_flow_total_fct / small_flow_count)
        print "Small flow FCT: %.4f" % (small_flow_total_fct / small_flow_count)

    small_flow_list.sort (key=lambda x: x.fct)
    small_index_99 = int(len(small_flow_list) * 0.99)
    if small_flow_list:
        small_flow_fct_99 = small_flow_list[small_index_99].fct
        print "Small flow 99 FCT: %.4f" % small_flow_fct_99
        #print "Max small flow Id: %i" % max_small_flow_id
    else:
        small_flow_fct_99 = -1.0

    if middle_flow_count == 0:
        print "No middle flows"
    else:
        avg_middle_fct = (middle_flow_total_fct / middle_flow_count)
        print "Middle flow FCT: %.4f" % (middle_flow_total_fct / middle_flow_count)

    if large_flow_count == 0:
        print "No large flows"
    else:
        avg_large_fct = (large_flow_total_fct / large_flow_count)
        avg_large_throughput = total_large_throughput / large_flow_count
        print "Large flow FCT: %.4f" % (large_flow_total_fct / large_flow_count)

    return {'avg_fct': avg_fct, 'avg_small_fct': avg_small_fct, \
    'avg_large_fct': avg_large_fct, 'avg_middle_fct': avg_middle_fct, \
    'small_flow_99': small_flow_fct_99, \
    'flow_99' : flow_fct_99, 'total_tx' : total_packets, \
    'total_lost' : total_lost_packets, \
    'total_rx' : total_rx_packets, 'flow_count' : flow_count, \
    'large_throughput' : avg_large_throughput}


def main (argv):
    files = glob.glob (argv[1])
    total_fct = 0
    total_large_fct = 0
    total_middle_fct = 0
    total_small_fct = 0
    total_small_flow_99 = 0
    total_flow_99 = 0
    total_tx = 0
    total_rx = 0
    total_lost = 0
    flow_count = 0
    total_large_throughput = 0
    print files
    for fileName in files:
        print (fileName)
        result = parse (fileName)
        total_fct += result['avg_fct']
        if result['avg_large_fct']>0 :
            total_large_fct += result['avg_large_fct']
        if result['avg_middle_fct']>0 :
            total_middle_fct += result['avg_middle_fct']
        if result['avg_small_fct']>0 :
            total_small_fct += result['avg_small_fct']
        if result['small_flow_99']>0 :
            total_small_flow_99 += result['small_flow_99']
        total_flow_99 += result['flow_99']
        total_tx += result['total_tx']
        total_rx += result['total_rx']
        total_lost += result['total_lost']
        flow_count += result['flow_count']
        total_large_throughput += result['large_throughput']
        print ('')

    if len(files)>1 :
        print "Summary of All Files"
        print "--------------------"
        print "File #: %i" % len(files)
        print "Total Flow #: %i" % (flow_count / len(files))
        print "Total TX Packets: %i" % (total_tx / len(files))
        print "Total RX Packets: %i" % (total_rx / len(files))
        print "Total Lost Packets: %i" % (total_lost / len(files))
        print "Total Lost Rate: %.5f" % (total_lost / total_tx)
        print "All flow FCT: %.4f" % (total_fct / len(files))
        print "All flow 99 FCT: %.4f" % (total_flow_99 / len(files))
        print "Small flow FCT: %.4f" % (total_small_fct / len(files))
        print "Small flow 99 FCT: %.4f" % (total_small_flow_99 / len(files))
        print "Middle flow FCT: %.4f" % (total_middle_fct / len(files))
        print "Large flow FCT: %.4f" % (total_large_fct / len(files))
        print "Large flow throughput: %.4f " % (total_large_throughput / len(files))


if __name__ == '__main__':
    main(sys.argv)
