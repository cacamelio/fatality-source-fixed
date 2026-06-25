#pragma once

class CCLCMsg_Move
{
private:
	virtual ~CCLCMsg_Move();
	char pad[8]{};
public:
	int backup_commands{};
	int new_commands{};
};

namespace tickbase
{
	void reset();
	bool is_ready();
	void adjust_limit_dynamic(CUserCmd* cmd = globals::current_cmd);
	bool holds_tick_base_weapon();
	bool attempt_shift_back(bool& send_packet);
	void revert_shift_back();
	void fill_fake_commands();
	bool apply_static_configuration();
	int determine_optimal_shift();
	int determine_optimal_limit();
	float get_adjusted_time();
	int compute_current_limit(int command_number = interfaces::client_state()->lastoutgoingcommand + interfaces::client_state()->chokedcommands + 1);
	void on_send_command(int command_number = interfaces::client_state()->lastoutgoingcommand + interfaces::client_state()->chokedcommands + 1);
	void on_finish_command(bool send_packet);
	void on_recharge(int command_number);
	void on_runcmd(const CUserCmd* cmd, int& tickbase);

	inline int to_recharge = {}, to_shift = {}, to_adjust = {}, delay_shift = -1;
	inline bool force_choke = {}, force_unchoke = {}, skip_next_adjust = {}, fast_fire = {}, hide_shot = {}, post_shift = {}, keep_config_changed{};
};