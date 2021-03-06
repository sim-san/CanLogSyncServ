
#include "CanBus.h"

CanBus::CanBus(id_t bus_id, Can&& can, const std::vector<std::pair<canid_t, std::vector<DBCSignal_Wrapper>>>&& msgs)
		: _bus_id{bus_id}
		, _can{std::move(can)}
		, _time{0}
{
	for (const auto& msg : msgs)
	{
		std::vector<std::pair<void*, DBCSignal_Wrapper>> wrappers;
		for (const auto& sig_wrapper : msg.second)
		{
			wrappers.push_back(std::make_pair(nullptr, std::move(sig_wrapper)));
		}
		_msgs.insert(std::make_pair(msg.first, std::move(wrappers)));
	}
}
CanBus::CanBus(CanBus&& other)
	: _bus_id{other._bus_id}
	, _can{std::move(other._can)}
	, _msgs{std::move(other._msgs)}
	, _time{other._time.load()}
{
}
std::vector<Signal> CanBus::recv(std::chrono::microseconds timeout) const
{
	std::vector<Signal> result;
	auto frame = _can.recv(timeout);
	if (frame)
	{
		_time = std::chrono::duration_cast<std::chrono::microseconds>(frame->timestamp).count();
		auto iter_signals = _msgs.find(frame->raw_frame.can_id);
		if (iter_signals != _msgs.end())
		{
			const auto& raw = frame->raw_frame;
			//uint64_t data =
			//	*((uint64_t*)&raw.data[0]) | (*((uint64_t*)&raw.data[1]) << 8) | (*((uint64_t*)&raw.data[2]) << 16)
			//	| (*((uint64_t*)&raw.data[3]) << 24) | (*((uint64_t*)&raw.data[4]) << 32) | (*((uint64_t*)&raw.data[5]) << 40)
			//	| (*((uint64_t*)&raw.data[6]) << 48) | (*((uint64_t*)&raw.data[7]) << 56);
			uint64_t data = *((uint64_t*)&raw.data[0]);
			for (const auto& signal : iter_signals->second)
			{
				auto& dbc_sig = signal.second.dbc_signal;
				auto& dbc_mux_sig = signal.second.dbc_mux_signal;
				if (dbc_sig->multiplexer_indicator != dbcppp::Signal::Multiplexer::MuxValue ||
					dbc_mux_sig->raw_to_phys(dbc_mux_sig->decode(data)) ==
						dbc_sig->multiplexer_switch_value)
				{
					Signal sig;
					sig.id = signal.second.id;
					sig.value = dbc_sig->raw_to_phys(dbc_sig->decode(data));
					sig.dbc_signal = signal.second.dbc_signal;
					sig.timestamp = frame->timestamp;
					sig.bus_id = _bus_id;
					sig.user_data = signal.first;
					result.push_back(std::move(sig));
				}
			}
		}
	}
	return result;
}
bool CanBus::set_user_data(canid_t canid, Signal::id_t signal_id, void* user_data)
{
	auto msg_iter = _msgs.find(canid);
	if (msg_iter != _msgs.end())
	{
		auto sig_iter = std::find_if(msg_iter->second.begin(), msg_iter->second.end(),
			[signal_id](const auto& sig) { return sig.second.id == signal_id; });
		if (sig_iter != msg_iter->second.end())
		{
			sig_iter->first = user_data;
			return true;
		}
	}
	return false;
}
void* CanBus::get_and_unset_user_data(canid_t canid, Signal::id_t signal_id)
{
	void* result = nullptr;
	auto msg_iter = _msgs.find(canid);
	if (msg_iter != _msgs.end())
	{
		auto sig_iter = std::find_if(msg_iter->second.begin(), msg_iter->second.end(),
			[signal_id](const auto& sig) { return sig.second.id == signal_id; });
		if (sig_iter != msg_iter->second.end())
		{
			result = sig_iter->first;
			sig_iter->first = nullptr;
		}
	}
	return result;
}
std::vector<std::pair<canid_t, Signal::id_t>> CanBus::canids_and_signal_ids() const
{
	std::vector<std::pair<canid_t, Signal::id_t>> result;
	for (const auto& msg : _msgs)
	{
		for (const auto& sig : msg.second)
		{
			result.push_back(std::make_pair(msg.first, sig.second.id));
		}
	}
	return result;
}
std::chrono::microseconds CanBus::time() const
{
	return std::chrono::microseconds{_time.load()};
}
CanBus::id_t CanBus::id() const
{
	return _bus_id;
}
