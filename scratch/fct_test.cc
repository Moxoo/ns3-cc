#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FCT_TEST");

std::stringstream filePlotQueue;
std::stringstream filePlotThroughput;

static bool firstCwnd = true;
static bool firstSshThr = true;
static bool firstRtt = true;
static bool firstRto = true;
static Ptr<OutputStreamWrapper> cWndStream;
static Ptr<OutputStreamWrapper> ssThreshStream;
static Ptr<OutputStreamWrapper> rttStream;
static Ptr<OutputStreamWrapper> rtoStream;
static uint32_t cWndValue;
static uint32_t ssThreshValue;
static double interval = 0.0005;
uint64_t flowRecvBytes = 0;

static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;

  if (!firstSshThr)
    {
      *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue << std::endl;
    }
}

static void
SsThreshTracer (uint32_t oldval, uint32_t newval)
{
  if (firstSshThr)
    {
      *ssThreshStream->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr = false;
    }
  *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue = newval;

  if (!firstCwnd)
    {
      *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue << std::endl;
    }
}

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
RtoTracer (Time oldval, Time newval)
{
  if (firstRto)
    {
      *rtoStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRto = false;
    }
  *rtoStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}


static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name)
{
  AsciiTraceHelper ascii;
  ssThreshStream = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&SsThreshTracer));
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceRto (std::string rto_tr_file_name)
{
  AsciiTraceHelper ascii;
  rtoStream = ascii.CreateFileStream (rto_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/1/$ns3::TcpL4Protocol/SocketList/0/RTO", MakeCallback (&RtoTracer));
}

void
CheckQueueSize (Ptr<QueueDisc> queue, std::string filePlotQueue)
{
  uint32_t qSize = StaticCast<FifoQueueDisc> (queue)->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.001), &CheckQueueSize, queue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

void
ThroughputPerSecondA(Ptr<PacketSink> sinkApp,std::string filePlotThrough)
{
	uint32_t totalRecvBytes = sinkApp->GetTotalRx();
	uint32_t currentPeriodRecvBytes = totalRecvBytes - flowRecvBytes;

	flowRecvBytes = totalRecvBytes;

	Simulator::Schedule (Seconds(interval), &ThroughputPerSecondA, sinkApp, filePlotThrough);
	std::ofstream fPlotThroughput (filePlotThrough.c_str (), std::ios::out | std::ios::app);
	fPlotThroughput << Simulator::Now().GetSeconds() << " " << currentPeriodRecvBytes * 8 / (interval * 1000000) << std::endl;
}

int main (int argc, char *argv[])
{
  // Configure information
  std::string prefix_file_name = "FCT_TEST";
  uint32_t sendNum = 21;
  std::string transport_port = "TcpDctcp";//  Or "TcpCubic"
  std::string queue_disc_type = "RedQueueFrontDisc";
  std::string queue_limit = "400p";//almost 4MB buffer
  double K = 65;// 20 for 1Gbps, 65 for 10Gbps
  std::string bandwidth = "1000Mbps";//1Gbps,10Gbps
  std::string delay = "0.01ms";
  std::string bottleneck_bw = "10000Mbps";//1Gbps,10Gbps
  std::string bottleneck_delay = "0.01ms";
  uint64_t data_mbytes = 0;
  uint64_t data_mbytes_short = 1048576;//1MB
  double minRto = 0.005;//5ms
  uint32_t initialCwnd = 1;
  uint32_t PACKET_SIZE = 1000;
  double start_time = 0;
  double stop_time = 0.21;

  double start_time_short;
  srand((unsigned)time(NULL));
  start_time_short = 0.08 + rand()/double(RAND_MAX)/10;
  double stop_time_short = stop_time;

  bool tracing = true;
  bool printRedStats_tail = false;
  bool printRedStats_front = false;
  bool printFifoStats = false;
  bool pcap = false;

  // Create directory information
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];
  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
  std::string currentTime (buffer);

  CommandLine cmd;
  cmd.AddValue ("sendNum",     "Number of left and right side leaf nodes", sendNum);
  cmd.AddValue ("bandwidth", "Access bandwidth", bandwidth);
  cmd.AddValue ("delay", "Access delay", delay);
  cmd.AddValue ("bottleneck_bw", "Bottleneck bandwidth", bottleneck_bw);
  cmd.AddValue ("bottleneck_delay", "Bottleneck delay", bottleneck_delay);
  cmd.AddValue ("PACKET_SIZE", "Packet size", PACKET_SIZE);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit, 0 means infinite", data_mbytes);
  cmd.AddValue ("start_time", "Start Time", start_time);
  cmd.AddValue ("stop_time", "Stop Time", stop_time);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpDctcp, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_port);
  cmd.Parse (argc,argv);

  // To enable logging
  LogComponentEnable("FCT_TEST", LOG_LEVEL_INFO);
  //LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
  //LogComponentEnable("RedQueueDisc", LOG_LEVEL_ALL);
  //LogComponentEnable("RedQueueFrontDisc", LOG_LEVEL_ALL);
  //LogComponentEnable("FifoQueueDisc", LOG_LEVEL_LOGIC);
  //LogComponentEnable("Ipv4QueueDiscItem", LOG_LEVEL_DEBUG);

  NS_LOG_INFO ("Configure TcpSocket");
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (PACKET_SIZE));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initialCwnd));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (Seconds (minRto)));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (true));
  Config::SetDefault("ns3::TcpSocketBase::EcnMode", EnumValue(1));
  Config::SetDefault("ns3::"+queue_disc_type+"::UseEcn", BooleanValue (true));
  Config::SetDefault("ns3::"+queue_disc_type+"::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
  Config::SetDefault("ns3::"+queue_disc_type+"::MeanPktSize", UintegerValue (PACKET_SIZE));
  Config::SetDefault("ns3::"+queue_disc_type+"::UseHardDrop", BooleanValue (false));


  if(transport_port.compare("TcpDctcp") == 0)
  {
	  NS_LOG_INFO ("Configure TcpDctcp And Red");
	  Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDctcp::GetTypeId()));
	  Config::SetDefault("ns3::"+queue_disc_type+"::QW", DoubleValue (1));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MinTh", DoubleValue (K));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MaxTh", DoubleValue (K));
	  Config::SetDefault("ns3::"+queue_disc_type+"::Gentle", BooleanValue (false));
  }

  // Create sendNum, receiver, and switches
  NodeContainer switches;
  switches.Create(1);
  NodeContainer senders;
  senders.Create(sendNum);
  NodeContainer receiver;
  receiver.Create(1);

  // Configure channel attributes
  PointToPointHelper ptpLink;
  ptpLink.SetDeviceAttribute("DataRate", StringValue(bandwidth));
  ptpLink.SetChannelAttribute("Delay", StringValue(delay));

  PointToPointHelper neckLink;
  neckLink.SetDeviceAttribute("DataRate", StringValue(bottleneck_bw));
  neckLink.SetChannelAttribute("Delay", StringValue(bottleneck_delay));

  // Install InternetStack
  InternetStackHelper stack;
  stack.InstallAll();

  // Configure TrafficControlHelper
  TrafficControlHelper tchRed,tchFiFo;
  tchRed.SetRootQueueDisc("ns3::"+queue_disc_type);

  // Configure Ipv4AddressHelper
  Ipv4AddressHelper address;
  address.SetBase("10.0.0.0", "255.255.255.0");

  // Configure net devices in nodes and connect sendNum with switches
  for(uint32_t i = 0; i<sendNum; i++)
  {
	  NetDeviceContainer devices;
	  devices = ptpLink.Install(senders.Get(i), switches.Get(0));
	  //tchRed.Install(devices);
	  address.NewNetwork();
	  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  }

  // Connect switches with receiver
  NetDeviceContainer devices;
  devices = neckLink.Install(switches.Get(0), receiver.Get(0));

  // Install queueDiscs in switch
  QueueDiscContainer queueDiscs;
  if(queue_disc_type.compare("RedQueueFrontDisc") == 0)
  {
	  NS_LOG_INFO ("Install "+queue_disc_type);
	  queueDiscs = tchRed.Install(devices);
	  printRedStats_front = true;
  }
  else if(queue_disc_type.compare("RedQueueDisc") == 0)
  {
	  NS_LOG_INFO ("Install "+queue_disc_type);
	  queueDiscs = tchRed.Install(devices);
	  printRedStats_tail = true;
  }

  address.NewNetwork();
  Ipv4InterfaceContainer interfaces = address.Assign(devices);
  Ipv4InterfaceContainer sink_interfaces;
  sink_interfaces.Add(interfaces.Get(1));

  // Configure routing
  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Configure port/address and install application, establish connection
  NS_LOG_INFO ("Build connections");
  uint16_t port = 50000;

  // long flow
  for(uint16_t i=0; i<senders.GetN()-1; i++)
  {
	  // BulkSend
	  BulkSendHelper ftp("ns3::TcpSocketFactory", InetSocketAddress(sink_interfaces.GetAddress(0,0),port));
	  ftp.SetAttribute("SendSize", UintegerValue(PACKET_SIZE));
	  ftp.SetAttribute("MaxBytes", UintegerValue(data_mbytes*0));
	  ApplicationContainer sourceApp = ftp.Install(senders.Get(i));
	  sourceApp.Start(Seconds(start_time));
	  sourceApp.Stop(Seconds(stop_time));
  }

  PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), port));
  //sinkHelper.SetAttribute("Protocol", TypeIdValue(TcpSocketFactory::GetTypeId()));
  ApplicationContainer sinkApp = sinkHelper.Install(receiver.Get(0));
  sinkApp.Start(Seconds(start_time));
  sinkApp.Stop(Seconds(stop_time));

  // short flow
  BulkSendHelper ftpShort("ns3::TcpSocketFactory", InetSocketAddress(sink_interfaces.GetAddress(0,0),port+1));
  ftpShort.SetAttribute("SendSize", UintegerValue(PACKET_SIZE));
  ftpShort.SetAttribute("MaxBytes", UintegerValue(data_mbytes_short));
  ApplicationContainer sourceAppShort = ftpShort.Install(senders.Get(sendNum-1));
  sourceAppShort.Start(Seconds(start_time_short));
  sourceAppShort.Stop(Seconds(stop_time_short));

  PacketSinkHelper sinkHelperShort("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), port+1));
  ApplicationContainer sinkAppShort = sinkHelperShort.Install(receiver.Get(0));
  sinkAppShort.Start(Seconds(start_time_short));
  sinkAppShort.Stop(Seconds(stop_time_short));

  // Collect data
  std::string dir = "results/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
  std::cout << "Data directory:" << dir << std::endl;
  if (tracing)
  {
	  std::string dirToSave = "mkdir -p " + dir;
	  system (dirToSave.c_str ());
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceCwnd, dir + "cwnd.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceSsThresh, dir + "ssth.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRtt, dir + "rtt.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRto, dir + "rto.data");

	  // Get queue size
	  filePlotQueue << dir << "/" << "queue-size.plotme";
	  //remove (filePlotQueue.str ().c_str());
	  Ptr<QueueDisc> queue = queueDiscs.Get(0);
	  Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());

	  // Get throughput
	  filePlotThroughput << dir << "/" << "throughputA.plotme";
	  //remove (filePlotThroughput.str ().c_str());
	  Simulator::ScheduleNow (&ThroughputPerSecondA, sinkApp.Get(0)->GetObject<PacketSink>(), filePlotThroughput.str ());
  }

  if(pcap)
  {
	  ptpLink.EnablePcapAll(prefix_file_name, true);
	  neckLink.EnablePcapAll(prefix_file_name, true);
  }

  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  // Write configure information to file
  std::ofstream myfile;
  myfile.open (dir + "config.txt", std::fstream::in | std::fstream::out | std::fstream::app);
  myfile << "sendNum " << sendNum << "\n";
  myfile << "transport_prot " << transport_port << "\n";
  myfile << "queuedisc " << queue_disc_type << "\n";
  myfile << "queuelimit " << queue_limit << "\n";
  myfile << "threshold for DCTCP " << K << "\n";
  myfile << "access bandwidth " << bandwidth << "\n";
  myfile << "access delay  " << delay << "\n";
  myfile << "neck bandwidth " << bottleneck_bw << "\n";
  myfile << "neck delay  " << bottleneck_delay << "\n";
  myfile << "data for transmission " << data_mbytes << "\n";
  myfile << "minRto " << minRto << "\n";
  myfile << "initialCwnd  " << initialCwnd << "\n";
  myfile << "PACKET_SIZE " << PACKET_SIZE << "\n";
  myfile << "start time " << start_time << "\n";
  myfile << "stop time " << stop_time << "\n";

  myfile << "short flow bytes " << data_mbytes_short << "\n";
  myfile << "short flow start time " << start_time_short << "\n";
  myfile << "short flow stop time " << stop_time_short << "\n";
  myfile << "\n";
  myfile.close();

  //AnimationInterface anim("dctcpVScubic.xml");
  Simulator::Stop (Seconds (stop_time));
  Simulator::Run ();

  // Get information from FlowMonitor
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
      std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
      std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
      std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 * 1000000000 / NanoSeconds(i->second.timeLastRxPacket-i->second.timeFirstTxPacket) / 1000 / 1000  << " Mbps\n";
      std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
      std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 * 1000000000 / NanoSeconds(i->second.timeLastRxPacket-i->second.timeFirstTxPacket) / 1000 / 1000  << " Mbps\n";
      std::cout << "  FCT:  " << NanoSeconds(i->second.timeLastRxPacket-i->second.timeFirstTxPacket)  << "\n";

      if(i==(stats.end().operator --().operator --()))
      {
    	  myfile.open (dir + "config.txt", std::fstream::in | std::fstream::out | std::fstream::app);
    	  myfile << "FCT of short : " << NanoSeconds(i->second.timeLastRxPacket-i->second.timeFirstTxPacket) << "\n";
    	  myfile << "\n";
    	  myfile.close();
      }
    }

  if(printRedStats_tail)
  {
	  RedQueueDisc::Stats st = StaticCast<RedQueueDisc> (queueDiscs.Get(0))->GetStats();
	  NS_LOG_INFO("*** RED Static from switch queue ***");
	  NS_LOG_INFO(st.nTotalReceivedPackets << " : packets received totally");
	  NS_LOG_INFO(st.nTotalEnqueuedPackets << " : packets enqueued totally");
	  NS_LOG_INFO(st.nTotalDroppedPackets << " : packets dropped totally");
	  NS_LOG_INFO(st.nTotalDroppedPacketsBeforeEnqueue << " : packets dropped before enqueue totally");
	  NS_LOG_INFO(st.nTotalMarkedPackets << " : packets marked totally");

	  double drop_ratio = double(st.nTotalDroppedPackets)/st.nTotalReceivedPackets;
	  double mark_ratio = double (st.nTotalMarkedPackets)/st.nTotalReceivedPackets;
	  myfile.open (dir + "config.txt", std::fstream::in | std::fstream::out | std::fstream::app);
	  myfile << "*** RED Static from switch queue ***" << "\n";
	  myfile << "received packets : " << st.nTotalReceivedPackets << "\n";
	  myfile << "euqueued packets : " << st.nTotalEnqueuedPackets << "\n";
	  myfile << "dropped packets : " << st.nTotalDroppedPackets << "\n";
	  myfile << "marked packets : " << st.nTotalMarkedPackets << "\n";
	  myfile << "dropping ratio : " << drop_ratio << "\n";
	  myfile << "marking ratio : " << mark_ratio << "\n";
	  myfile.close();
  }
  if(printRedStats_front)
  {
	  RedQueueFrontDisc::Stats st = StaticCast<RedQueueFrontDisc> (queueDiscs.Get(0))->GetStats();
	  NS_LOG_INFO("*** RED Static from switch queue ***");
	  NS_LOG_INFO(st.nTotalReceivedPackets << " : packets received totally");
	  NS_LOG_INFO(st.nTotalEnqueuedPackets << " : packets enqueued totally");
	  NS_LOG_INFO(st.nTotalDroppedPackets << " : packets dropped totally");
	  NS_LOG_INFO(st.nTotalDroppedPacketsBeforeEnqueue << " : packets dropped before enqueue totally");
	  NS_LOG_INFO(st.nTotalMarkedPackets << " : packets marked totally");

	  double drop_ratio = double(st.nTotalDroppedPackets)/st.nTotalReceivedPackets;
	  double mark_ratio = double (st.nTotalMarkedPackets)/st.nTotalReceivedPackets;
	  myfile.open (dir + "config.txt", std::fstream::in | std::fstream::out | std::fstream::app);
	  myfile << "*** RED Static from switch queue ***" << "\n";
	  myfile << "received packets : " << st.nTotalReceivedPackets << "\n";
	  myfile << "euqueued packets : " << st.nTotalEnqueuedPackets << "\n";
	  myfile << "dropped packets : " << st.nTotalDroppedPackets << "\n";
	  myfile << "marked packets : " << st.nTotalMarkedPackets << "\n";
	  myfile << "dropping ratio : " << drop_ratio << "\n";
	  myfile << "marking ratio : " << mark_ratio << "\n";
  }
  if(printFifoStats)
  {
	  FifoQueueDisc::Stats st = StaticCast<FifoQueueDisc> (queueDiscs.Get(0))->GetStats();
	  NS_LOG_INFO("*** FIFO Static from switch queue ***");
	  NS_LOG_INFO(st.nTotalReceivedPackets << " : packets received totally");
	  NS_LOG_INFO(st.nTotalEnqueuedPackets << " : packets enqueued totally");
	  NS_LOG_INFO(st.nTotalDroppedPackets << " : packets dropped totally");
	  NS_LOG_INFO(st.nTotalDroppedPacketsBeforeEnqueue << " : packets dropped before enqueue totally");
  }

  Simulator::Destroy ();
  return 0;
}
