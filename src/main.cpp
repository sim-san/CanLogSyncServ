
#include <zmq.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <boost/program_options.hpp>
#include <Vector/DBC.h>

#include "ConfigParser.h"
#include "Can.h"
#include "CanSync.h"
#include "CanBus.h"
#include "ZmqServer.h"

int main(int argc, char** argv)
{
	namespace po = boost::program_options;
	po::options_description desc;
	desc.add_options()
		("help", "produce help message")
		("config", po::value<std::string>()->default_value(""), "config file")
		("dbc", po::value<std::string>()->required(), "DBC file")
		("ipc_link", po::value<std::vector<std::string>>()->multitoken()->required(),
			"List of IPC links. E.g. ipc:///tmp/weather.ipc or tcp://*:5556, "
			"for a exhaustive list of supported protocols, please go to http://wiki.zeromq.org/docs:features "
			"under the Protocols section.")
		("iface", po::value<std::vector<std::string>>()->multitoken()->required(), "list of CAN interfaces")
		("sample_rate", po::value<uint64_t>()->default_value(5000), "sample rate in microseconds")
		("signal", po::value<std::vector<std::string>>()->multitoken(), "list of signals to log");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	if (vm.empty() || vm.count("help"))
	{
		std::cout << "usage: CanLogSyncServ "
			"--config=<config_file> "
			"--dbc=<dbc_file> "
			"--iface=<iface>... "
			"--ipc_link=<ipc_link>... "
			"[--sample_rate=<sample_rate>]"
			<< std::endl;
		std::cout << desc << std::endl;
		return 0;
	}
	po::notify(vm);
	const auto& config_file_path = vm["config"].as<std::string>();
	const auto& dbc_file_path    = vm["dbc"].as<std::string>();
	const auto& ipc_links        = vm["ipc_link"].as<std::vector<std::string>>();
	const auto& ifaces           = vm["iface"].as<std::vector<std::string>>();
	const auto& sample_rate      = vm["sample_rate"].as<uint64_t>();
	const auto& cmd_signals      = vm["signal"];
    
	ConfigParser cfg{cmd_signals.empty() ? std::vector<std::string>{} : cmd_signals.as<std::vector<std::string>>(), config_file_path};
	const auto& cfg_sigs = cfg.signals();

	Vector::DBC::Network network;
	{
		std::ifstream dbc_file{dbc_file_path};
		dbc_file >> network;
	}

	std::vector<std::pair<canid_t, std::vector<DBCSignal_Wrapper>>> msgs;
	std::vector<CanBus> can_buses;
	{
		for (const auto& msg : network.messages)
		{
			std::vector<DBCSignal_Wrapper> wrappers;
			std::shared_ptr<Vector::DBC::Signal> mux_sig;
			auto sig_mux_iter = std::find_if(msg.second.signals.begin(), msg.second.signals.end(),
				[](const auto& sig)
				{
					return sig.second.multiplexor == Vector::DBC::Signal::Multiplexor::MultiplexorSwitch;
				});
		      	if (sig_mux_iter != msg.second.signals.end())
			{
				mux_sig = std::make_shared<Vector::DBC::Signal>(sig_mux_iter->second);
			}
			for (const auto& sig : msg.second.signals)
			{
				auto iter = std::find_if(cfg_sigs.begin(), cfg_sigs.end(),
					[&](const ConfigParser::CfgSignal& cfg_sig)
					{
						return cfg_sig.canid == msg.first && sig.second.name == cfg_sig.signal_name;
					});
				if (iter != cfg_sigs.end())
				{
					DBCSignal_Wrapper sig_wrapper;
					sig_wrapper.id = iter->signal_id;
					sig_wrapper.dbc_signal = sig.second;
					if (sig.second.multiplexor == Vector::DBC::Signal::Multiplexor::MultiplexedSignal)
					{
						sig_wrapper.dbc_mux_signal = mux_sig;
					}
					wrappers.push_back(sig_wrapper);
				}
			}
			if (wrappers.size())
			{
				msgs.push_back(std::make_pair(msg.second.id, std::move(wrappers)));
			}
		}

		for (const auto& iface : ifaces)
		{
			can_buses.emplace_back(0, Can{iface}, std::move(msgs));
		}
	}
	CanSync can_sync{std::chrono::microseconds{sample_rate}, std::move(can_buses)};
	
	ZmqServer zmq_server{ipc_links};
	can_sync.sub([&zmq_server](const auto& p1, const auto& p2) { zmq_server.cb_sub(p1, p2); });
	can_sync.start();

	while (can_sync.running())
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	return 0;
}
