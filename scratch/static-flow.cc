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

using namespace ns3;
using namespace std;

// Constants.
#define ENABLE_PCAP      false     // Set to "true" to enable pcap
#define ENABLE_TRACE     false     // Set to "true" to enable trace
//#define QUEUE_SIZE       333333   // Packets (50MB=33.3kMTU)*1/2/4/10/20/40/80
#define START_TIME       0.0       // Seconds
#define STOP_TIME        5.0       // Seconds
#define S_TO_R_BW        "10Gbps" // Server to router
#define S_TO_R_DELAY     "10ms"
#define R_TO_C_BW        "10Gbps"  // Router to client (bttlneck)
#define R_TO_C_DELAY     "10ms"
#define PACKET_SIZE      1448      // Bytes.
#define FLOW_NUM         200   // n of n-to-1 (incast degree)

// Uncomment one of the below.
//#define TCP_PROTOCOL     "ns3::TcpCubic"
//#define TCP_PROTOCOL     "ns3::TcpBbr"

int drop_count=0;

void PrintTimeNow() {
  std::cout << "Time: ";
  std::cout << Simulator::Now().GetSeconds() << std::endl;
  Simulator::Schedule(MilliSeconds(100), &PrintTimeNow);
}

static void
QueueDropTrace (Ptr<const Packet> p)
{
  drop_count++;
  std::cout << "Time: " << Simulator::Now ().GetSeconds () << ". Packet drop #: " << drop_count << std::endl;
}

void
PacketsInQueueTrace (Ptr<OutputStreamWrapper> stream, unsigned int oldValue, unsigned int newValue)
{
  *stream->GetStream() << Simulator::Now().GetSeconds()  << " " << oldValue << " " << newValue << std::endl;
  std::cout << "Time: " << Simulator::Now().GetSeconds()  << ". Queue length from " << oldValue << " to " << newValue << std::endl;
}

// For logging. 

NS_LOG_COMPONENT_DEFINE ("main");
/////////////////////////////////////////////////
int main (int argc, char *argv[]) {

  srand (time(NULL));

  CommandLine cmd;
  string tcp_protocol="ns3::TcpBbr";
  int buffer_size = 1000;
  cmd.AddValue("protocol", "Transport protocol in use", tcp_protocol);
  cmd.AddValue("bSize", "Buffer size in packets", buffer_size);
  cmd.Parse (argc, argv);

  string file_prefix = tcp_protocol+"-buf"+to_string(buffer_size);

  /////////////////////////////////////////
  // Turn on logging for this script.
  // Note: for BBR', other components that may be
  // of interest include "TcpBbr" and "BbrState".
  LogComponentEnable("main", LOG_LEVEL_INFO);

  /////////////////////////////////////////
  // Setup environment
  Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                     StringValue(tcp_protocol));

  // Report parameters.
  NS_LOG_INFO("TCP protocol: " << tcp_protocol);
  NS_LOG_INFO("Flow #: " << FLOW_NUM);
  NS_LOG_INFO("Server to Router Bwdth: " << S_TO_R_BW);
  NS_LOG_INFO("Server to Router Delay: " << S_TO_R_DELAY);
  NS_LOG_INFO("Router to Client Bwdth: " << R_TO_C_BW);
  NS_LOG_INFO("Router to Client Delay: " << R_TO_C_DELAY);
  NS_LOG_INFO("Packet size (bytes): " << PACKET_SIZE);
  
  // Set segment size (otherwise, ns-3 default is 536).
  Config::SetDefault("ns3::TcpSocket::SegmentSize",
                     UintegerValue(PACKET_SIZE)); 

  // Turn off delayed ack (so, acks every packet).
  // Note, BBR' still works without this.
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(0));

  // Send buffer and Recv buffer should be large enough for high BDP network.
  // If FLOW_NUM>10, should reduce simulation memory usage. 
  if (FLOW_NUM>10) {
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(10000000000/FLOW_NUM));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(10000000000/FLOW_NUM));
  } else {
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1000000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1000000000));
  }
 
  // More config.
  // If BDP>>10, Try use a larger initial window, e.g., 40 pkts, to speed up simulation.
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (500)));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (100)));
  Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MilliSeconds (300)));

  /////////////////////////////////////////
  // Create nodes.
  NS_LOG_INFO("Creating nodes.");
  NodeContainer nodes;  // 0-(n-1)=source, n=router, n+1=sink
  nodes.Create(FLOW_NUM+2);

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
  NS_LOG_INFO("Router queue size: "<< buffer_size);
  p2p.SetQueue("ns3::DropTailQueue",
               "Mode", StringValue ("QUEUE_MODE_PACKETS"),
               "MaxPackets", UintegerValue(buffer_size));
  vector<NetDeviceContainer> devices;
  for (int i=0; i<FLOW_NUM; i++)
    devices.push_back(p2p.Install(s_to_r[i]));

  // Router to Client.
  p2p.SetDeviceAttribute("DataRate", StringValue (R_TO_C_BW));
  p2p.SetChannelAttribute("Delay", StringValue (R_TO_C_DELAY));
  p2p.SetDeviceAttribute ("Mtu", UintegerValue(mtu));
  p2p.SetQueue("ns3::DropTailQueue",
               "Mode", StringValue ("QUEUE_MODE_PACKETS"),
               "MaxPackets", UintegerValue(buffer_size));
  NetDeviceContainer devices2 = p2p.Install(r_to_n1);

  AsciiTraceHelper asciiTraceHelper;
  Ptr<Queue<Packet> > queue = StaticCast<PointToPointNetDevice> (devices2.Get (0))->GetQueue ();
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (file_prefix+"-queue.tr");
  queue->TraceConnectWithoutContext ("PacketsInQueue", MakeBoundCallback (&PacketsInQueueTrace, stream));
  queue->TraceConnectWithoutContext ("Drop", MakeCallback (&QueueDropTrace));

  /////////////////////////////////////////
  // Install Internet stack.
  NS_LOG_INFO("Installing Internet stack.");
  InternetStackHelper internet;
  Ipv4GlobalRoutingHelper globalRoutingHelper;
  internet.SetRoutingHelper (globalRoutingHelper);
  internet.Install(nodes);
  
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
    AsciiTraceHelper ath;
    p2p.EnableAsciiAll(ath.CreateFileStream(file_prefix+"-trace.tr"));
  }  
  if (ENABLE_PCAP) {
    NS_LOG_INFO("Enabling pcap files.");
    p2p.EnablePcapAll(file_prefix+"-shark", true);
  }

  Simulator::ScheduleNow(&PrintTimeNow);
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
  double tput_sum = 0.0;
  vector<double> tput(FLOW_NUM, 0.0);
  for (int i=0; i<FLOW_NUM; i++) {
    byte_sum += p_sink[i]->GetTotalRx();
    tput[i] = p_sink[i]->GetTotalRx() / (STOP_TIME - start_time[i]);
    tput[i] *= 8;          // Convert to bits.
    tput[i] /= 1000000.0;  // Convert to Mb/s
    tput_sum += tput[i];
  }
  NS_LOG_INFO("Total bytes received: " << byte_sum);
  NS_LOG_INFO("Throughput: " << tput_sum << " Mb/s");
  NS_LOG_INFO("Done.");

  // Done.
  Simulator::Destroy();
  return 0;
}
