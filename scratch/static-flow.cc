//           
// Network topology
//
//       n-to-1 dumbbell
//
// - Tracing of queues and packet receptions to file "*.tr" and
//   "*.pcap" when tracing is turned on.
//

// System includes.
#include <string>
#include <fstream>
#include <vector>

// NS3 includes.
#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
using namespace std;

#define ENABLE_PCAP      false     // Set to "true" to enable pcap
#define ENABLE_TRACE     false     // Set to "true" to enable trace
#define START_TIME       0.0       // Seconds
#define STOP_TIME        5.0       // Seconds
#define S_TO_R_BW        "10Gbps" // Server to router
#define S_TO_R_DELAY     "10ms"
#define R_TO_C_BW        "10Gbps"  // Router to client (bttlneck)
#define R_TO_C_DELAY     "10ms"
#define ENDHOST_BUFFER   1000000000 // should be at least one BDP + buffer_size
#define PACKET_SIZE      1448      // Bytes.
#define FLOW_NUM         200   // n of n-to-1 (incast degree)

void PeriodicPrint(vector<Ptr<PacketSink>> p_sink, double byte_sum, double tbyte_sum, Ptr<QueueDisc> qdisc) {
  double byte_sum_new = 0.0;
  for (int i=0; i<FLOW_NUM; i++)
    byte_sum_new += p_sink[i]->GetTotalRx();
  double goodput = (byte_sum_new - byte_sum) * 8 / 1000;
  
  double tbyte_sum_new = qdisc->GetStats().nTotalSentPackets*PACKET_SIZE;
  double throughput = 0.0;
  if (tbyte_sum_new >= tbyte_sum)
    throughput = (tbyte_sum_new - tbyte_sum) * 8 / 1000;
  else // in case of wraparound
    throughput = tbyte_sum_new * 8 / 1000;

  std::cout << "Time: " << Simulator::Now().GetSeconds();
  std::cout << " Goodput: " << goodput; // Mbps
  std::cout << " Throughput: " << throughput; // Mbps
  std::cout << " Queue: " << qdisc->GetNPackets();
  std::cout << " Packet drop: " << qdisc->GetStats().nTotalDroppedPackets << std::endl;
  Simulator::Schedule(MilliSeconds(1), &PeriodicPrint, p_sink, byte_sum_new, tbyte_sum_new, qdisc);
}

// For logging. 
NS_LOG_COMPONENT_DEFINE ("main");
/////////////////////////////////////////////////
int main (int argc, char *argv[]) {

  srand (time(NULL));

  CommandLine cmd;
  string tcp_protocol="ns3::TcpBbr";
  int buffer_size = 10;
  cmd.AddValue("protocol", "Transport protocol in use", tcp_protocol);
  cmd.AddValue("bSize", "Buffer size in packets", buffer_size);
  cmd.Parse (argc, argv);

  string file_prefix = tcp_protocol+"-buf"+to_string(buffer_size);

  /////////////////////////////////////////
  // Turn on logging for this script.
  // Note: for BBR', other components that may be
  // of interest include "TcpBbr" and "BbrState".
  LogComponentEnable("main", LOG_LEVEL_INFO);
  //LogComponentEnable("TcpBbr", LOG_LEVEL_INFO);
  //LogComponentEnable("BbrState", LOG_LEVEL_INFO);

  /////////////////////////////////////////
  // Setup environment
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(tcp_protocol));

  // Report parameters.
  NS_LOG_INFO("TCP protocol: " << tcp_protocol);
  NS_LOG_INFO("Flow #: " << FLOW_NUM);
  NS_LOG_INFO("Server to Router Bwdth: " << S_TO_R_BW);
  NS_LOG_INFO("Server to Router Delay: " << S_TO_R_DELAY);
  NS_LOG_INFO("Router to Client Bwdth: " << R_TO_C_BW);
  NS_LOG_INFO("Router to Client Delay: " << R_TO_C_DELAY);
  NS_LOG_INFO("Packet size (bytes): " << PACKET_SIZE);
  NS_LOG_INFO("Router queue size: "<< buffer_size);
  
  // Set segment size (otherwise, ns-3 default is 536).
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE)); 

  // Turn off delayed ack (so, acks every packet).
  // Note, BBR' still works without this.
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(0));

  // Send buffer and Recv buffer should be large enough for high BDP network.
  // If FLOW_NUM>10, should reduce simulation memory usage. 
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(ENDHOST_BUFFER/FLOW_NUM));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(ENDHOST_BUFFER/FLOW_NUM));
 
  // More config.
  // If BDP>>10, Try use a larger initial window, e.g., 40 pkts, to speed up simulation.
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (500)));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (200)));

  /////////////////////////////////////////
  // Create nodes.
  NS_LOG_INFO("Creating nodes.");
  NodeContainer nodes;  // 0-(n-1)=source, n=router, n+1=sink
  nodes.Create(FLOW_NUM+2);

  /////////////////////////////////////////
  // Install Internet stack.
  NS_LOG_INFO("Installing Internet stack.");
  InternetStackHelper internet;
  Ipv4GlobalRoutingHelper globalRoutingHelper;
  internet.SetRoutingHelper (globalRoutingHelper);
  internet.Install(nodes);

  /////////////////////////////////////////
  // Create channels.
  NS_LOG_INFO("Creating channels.");
  vector<NodeContainer> s_to_r;
  for (int i=0; i<FLOW_NUM; i++)
    s_to_r.push_back(NodeContainer(nodes.Get(i), nodes.Get(FLOW_NUM)));
  NodeContainer r_to_n1 = NodeContainer(nodes.Get(FLOW_NUM), nodes.Get(FLOW_NUM+1));

  /////////////////////////////////////////
  // Create links.
  NS_LOG_INFO("Creating links.");

  // Server to Router.
  int mtu = 1500;
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue (S_TO_R_BW));
  p2p.SetChannelAttribute("Delay", StringValue (S_TO_R_DELAY));
  p2p.SetDeviceAttribute ("Mtu", UintegerValue(mtu));
  vector<NetDeviceContainer> devices;
  for (int i=0; i<FLOW_NUM; i++)
    devices.push_back(p2p.Install(s_to_r[i]));

  // Router to Client.
  p2p.SetDeviceAttribute("DataRate", StringValue (R_TO_C_BW));
  p2p.SetChannelAttribute("Delay", StringValue (R_TO_C_DELAY));
  p2p.SetDeviceAttribute ("Mtu", UintegerValue(mtu));
  // the real packet dropping happens at the qdisc
  p2p.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(10));
  NetDeviceContainer devices2 = p2p.Install(r_to_n1);

  // Bottleneck queue to be monitored
  TrafficControlHelper tc;
  tc.SetRootQueueDisc ("ns3::PfifoFastQueueDisc", "Limit", UintegerValue (buffer_size));
  QueueDiscContainer bottleneck_qdisc = tc.Install (devices2);
  
  /////////////////////////////////////////
  // Add IP addresses.
  NS_LOG_INFO("Assigning IP Addresses.");
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.0.0", "255.255.0.0");
  vector<Ipv4InterfaceContainer> i0i1;
  for (int i=0; i<FLOW_NUM; i++)
    i0i1.push_back(ipv4.Assign(devices[i]));

  ipv4.SetBase("191.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i1i2 = ipv4.Assign(devices2);

  // To-be-optimized: time consuming when FLOW_NUM>1000
  NS_LOG_INFO("Populating Routing Tables.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  /////////////////////////////////////////
  // Create apps.
  NS_LOG_INFO("Creating applications.");
  NS_LOG_INFO(" Static flow transmission...");

  // Well-known port for server.
  uint16_t port = 911;  

  vector<Ptr<PacketSink>> p_sink;
  vector<double> start_time;
  for (int i=0; i<FLOW_NUM; i++) {
    // desynchronize the flow start time
    start_time.push_back(START_TIME+0.2*(float)rand()/RAND_MAX);

    // Source (at node i).
    BulkSendHelper source("ns3::TcpSocketFactory",
                          InetSocketAddress(i1i2.GetAddress(1), port+i));
    // Set the amount of data to send in bytes (0 for unlimited).
    source.SetAttribute("MaxBytes", UintegerValue(0));
    source.SetAttribute("SendSize", UintegerValue(PACKET_SIZE));
    ApplicationContainer apps = source.Install(nodes.Get(i));
    apps.Start(Seconds(start_time[i]));
    apps.Stop(Seconds(STOP_TIME));

    // Sink (at node n+1).
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port+i));
    apps = sink.Install(nodes.Get(FLOW_NUM+1));
    apps.Start(Seconds(START_TIME));
    apps.Stop(Seconds(STOP_TIME));
    p_sink.push_back(DynamicCast<PacketSink> (apps.Get(0))); // 4 stats
  }

  /////////////////////////////////////////
  // Setup tracing (as appropriate).
  if (ENABLE_TRACE) {
    NS_LOG_INFO("Enabling trace files.");
    AsciiTraceHelper asciiTraceHelper;
    p2p.EnableAsciiAll(asciiTraceHelper.CreateFileStream(file_prefix+"-trace.tr"));
  }  
  if (ENABLE_PCAP) {
    NS_LOG_INFO("Enabling pcap files.");
    p2p.EnablePcapAll(file_prefix+"-shark", true);
  }

  Simulator::ScheduleNow(&PeriodicPrint, p_sink, 0.0, 0.0, bottleneck_qdisc.Get (0));

  /////////////////////////////////////////
  // Run simulation.
  NS_LOG_INFO("Running simulation.");
  Simulator::Stop(Seconds(STOP_TIME));
  NS_LOG_INFO("Simulation time: [" << 
              START_TIME << "," <<
              STOP_TIME << "]");
  NS_LOG_INFO("---------------- Start -----------------------");
  Simulator::Run();
  NS_LOG_INFO("---------------- Stop ------------------------");

  /////////////////////////////////////////
  // Ouput stats.
  double byte_sum = 0.0;
  double goodput_sum = 0.0;
  vector<double> goodput(FLOW_NUM, 0.0);
  for (int i=0; i<FLOW_NUM; i++) {
    byte_sum += p_sink[i]->GetTotalRx();
    goodput[i] = p_sink[i]->GetTotalRx() / (STOP_TIME - START_TIME);
    goodput[i] *= 8;          // Convert to bits.
    goodput[i] /= 1000000.0;  // Convert to Mb/s
    goodput_sum += goodput[i];
  }
  double throughput = bottleneck_qdisc.Get (0)->GetStats().nTotalSentPackets*PACKET_SIZE 
                        * 8 / 1000000.0 / (STOP_TIME - START_TIME);
  NS_LOG_INFO("Total bytes received: " << byte_sum);
  NS_LOG_INFO("Throughput: " << throughput << " Mb/s");
  NS_LOG_INFO("Goodput: " << goodput_sum << " Mb/s");
  NS_LOG_INFO("Done.");

  // Done.
  Simulator::Destroy();
  return 0;
}
