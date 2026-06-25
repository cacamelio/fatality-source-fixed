#include "../include_cheat.h"

void tickbase::reset()
{
	to_recharge = to_shift = to_adjust = 0;
	delay_shift = -1;
	force_choke = force_unchoke = skip_next_adjust = fast_fire = hide_shot = post_shift = keep_config_changed = false;
}

bool tickbase::holds_tick_base_weapon()
{
	const auto wpn = local_weapon;
	if (!wpn)
		return false;

	const auto info = interfaces::weapon_system()->GetWpnData(wpn->get_weapon_id());

	if (!info)
		return false;

	return wpn->get_weapon_id() != WEAPON_TASER
		&& wpn->get_weapon_id() != WEAPON_FISTS
		&& wpn->get_weapon_id() != WEAPON_C4
		&& !wpn->is_grenade()
		&& wpn->GetClientClass()->m_ClassID != ClassId::CSnowball
		&& wpn->get_weapon_type() != WEAPONTYPE_UNKNOWN;
}

void tickbase::adjust_limit_dynamic(CUserCmd* cmd)
{
	const auto changed = apply_static_configuration();
	const auto ready = !to_shift && !post_shift && !force_choke;

	if (changed)
		keep_config_changed = force_unchoke = true;

	const auto wpn = local_weapon;
	if (!wpn || !ready || animations::most_recent.second != interfaces::client_state()->lastoutgoingcommand)
		return;

	const auto info = interfaces::weapon_system()->GetWpnData(wpn->get_weapon_id());
	if (!info)
		return;

	auto dont_recharge = wpn->is_grenade() && ((wpn->get_pin_pulled() || wpn->get_throw_time() != 0.f) ||
		aimbot::last_target != -1 || prediction::had_attack || cmd->weaponselect);
	if (dont_recharge)
		keep_config_changed = false;

	const auto diff_wpn = wpn->get_next_primary_attack() - interfaces::globals()->curtime;
	if (!dont_recharge && !changed && fast_fire && (wpn->is_shootable() || wpn->is_knife()) &&
		((info->cycle_time < .55f && diff_wpn > -.2f) || diff_wpn > .7f) && (wpn->is_knife() || !wpn->in_reload()))
		dont_recharge = true;

	const auto diff_player = local_player->get_next_attack() - interfaces::globals()->curtime;
	if (!dont_recharge && diff_player > .7f)
		dont_recharge = true;

	if (keep_config_changed)
		dont_recharge = false;

	if (dont_recharge)
		to_recharge = 0;

	const auto diff = determine_optimal_limit() - compute_current_limit();
	const auto standing = prediction::get_pred_info((cmd->command_number - 1)).velocity.Length() < 1.1f &&
		prediction::unpred_move.x == 0.f && prediction::unpred_move.y == 0.f;
	if (!dont_recharge && diff > 0 && (diff > 2 || standing))
	{
		to_recharge = diff;
		to_shift = 0;
	}
	else if (diff < 0)
	{
		to_recharge = 0;
		to_shift = -diff;
	}

	if (!diff)
		keep_config_changed = false;
}

bool tickbase::attempt_shift_back(bool& send_packet)
{
	const auto weapon = local_weapon;
	if (!weapon)
		return true;

	const auto is_revolver = weapon->get_weapon_id() == WEAPON_REVOLVER;

	const auto dont = (fast_fire || hide_shot) && is_revolver || globals::shot_command <= interfaces::client_state()->lastoutgoingcommand || to_shift > 0;

	if (compute_current_limit() > 3 && local_player->get_tickbase() > animations::lag.first && !dont)
	{
		const auto predicted_time = interfaces::globals()->curtime + ticks_to_time(compute_current_limit());
		const auto release_tick = time_to_ticks(weapon->get_next_secondary_attack() - predicted_time);

		skip_next_adjust = !is_revolver || release_tick > 1 && release_tick < 10 - interfaces::client_state()->chokedcommands;
		if (skip_next_adjust)
			send_packet = true;

		if (!resolver::shots.empty())
			resolver::shots.pop_back();

		prediction::take_shot(false);
		if (!is_revolver)
			prediction::take_secondary_shot(false);
		globals::shot_command = 0;

		misc::retract_peek = false;

		return false;
	}

	if (fast_fire)
	{
		to_shift = determine_optimal_shift();
		if (compute_current_limit() - to_shift < 3)
			to_shift = compute_current_limit();

		send_packet = true;
	}

	return true;
}

void tickbase::revert_shift_back()
{
	to_shift = 0;
}

void tickbase::on_send_command(int command_number)
{
	to_adjust = 0;

	const auto wpn = local_weapon;

	auto& p1 = prediction::get_pred_info(command_number);
	if (p1.sequence != command_number)
		return;

	p1.tickbase.sent_commands = interfaces::client_state()->chokedcommands + 1;

	if (((fast_fire && vars::aim.doubletap->get<bool>()) || hide_shot) &&
		wpn->get_weapon_id() != WEAPON_REVOLVER && antiaim::started_peek_fakelag() && !to_shift)
		skip_next_adjust = true;

	if (skip_next_adjust)
		interfaces::prediction()->get_predicted_commands() =
		clamp(interfaces::client_state()->lastoutgoingcommand - interfaces::client_state()->last_command_ack, 0,
			interfaces::prediction()->get_predicted_commands());
	else
		to_adjust = p1.tickbase.limit;

	for (auto i = interfaces::client_state()->lastoutgoingcommand + 1; i <= command_number; i++)
	{
		auto& p2 = prediction::get_pred_info(i);
		if (p2.sequence != i)
			continue;
		p2.tickbase.skip_fake_commands = skip_next_adjust;
	}

	compute_current_limit(command_number);
}

void tickbase::fill_fake_commands()
{
	const auto wpn = local_weapon;
	if (!wpn)
		return;

	const auto is_grenade = wpn->is_grenade();

	skip_next_adjust = false;
	for (auto i = 0; i < to_adjust; i++)
	{
		interfaces::client_state()->chokedcommands++;
		const auto sequence = interfaces::client_state()->lastoutgoingcommand + interfaces::client_state()->chokedcommands + 1;
		const auto cmd = &interfaces::input()->m_pCommands[sequence % 150];
		*cmd = *globals::current_cmd;
		cmd->command_number = sequence;
		if (!is_grenade)
			cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
		cmd->tick_count = globals::current_cmd->tick_count + 200 + i;
		misc::write_tick(cmd->command_number);
	}
}

void tickbase::on_runcmd(const CUserCmd* cmd, int& tickbase)
{
	const auto& p1 = prediction::get_pred_info(cmd->command_number);
	if (p1.sequence != cmd->command_number)
		return;

	auto to_adjust = 0;
	std::optional<bool> prev_skip_fake_commands;

	for (auto i = interfaces::client_state()->last_command_ack; i <= cmd->command_number; i++)
	{
		const auto& p2 = prediction::get_pred_info(i);
		if (p2.sequence != i)
			continue;

		if (p2.tickbase.invalid_commands > 0)
		{
			prev_skip_fake_commands = false;
			continue;
		}

		if (!prev_skip_fake_commands.has_value())
			prev_skip_fake_commands = p2.tickbase.skip_fake_commands;

		if (prev_skip_fake_commands != p2.tickbase.skip_fake_commands)
			to_adjust = (p2.tickbase.skip_fake_commands ? p2.tickbase.limit : -p2.tickbase.limit) + p2.tickbase.adjust;
		else
			to_adjust = 0;

		prev_skip_fake_commands = p2.tickbase.skip_fake_commands;
	}

	tickbase += to_adjust;
}

void tickbase::on_recharge(int command_number)
{
	auto& p = prediction::get_pred_info(command_number);
	p.reset();
	p.sequence = command_number;
	p.tickbase.invalid_commands++;
}

void tickbase::on_finish_command(bool send_packet)
{
	const auto cmd = interfaces::client_state()->lastoutgoingcommand + interfaces::client_state()->chokedcommands + 1;
	auto& p = prediction::get_pred_info(cmd);
	if (p.sequence != cmd)
		return;

	if (to_shift > 0)
		p.tickbase.extra_commands++;

	if (send_packet)
		fill_fake_commands();
}

bool tickbase::apply_static_configuration()
{
	const auto previous = fast_fire || hide_shot;

	if (vars::aim.fake_duck->get<bool>())
		fast_fire = hide_shot = false;
	else
	{
		fast_fire = vars::aim.doubletap->get<bool>();
		hide_shot = !fast_fire && vars::aim.silent->get<bool>();
	}

	return previous != (fast_fire || hide_shot);
}

int tickbase::determine_optimal_shift()
{
	const auto wpn = local_weapon;
	if (!wpn)
		return 0;

	const auto info = interfaces::weapon_system()->GetWpnData(wpn->get_weapon_id());
	if (!info)
		return 0;

	constexpr auto min_shift_amt = 4;
	const auto max_shift_amt = compute_current_limit();

	return clamp(wpn->is_secondary_attack_weapon() || wpn->get_weapon_id() == WEAPON_REVOLVER || vars::misc.peek_assist->get<bool>() ? max_shift_amt : time_to_ticks(info->cycle_time) - 1, min_shift_amt, max_shift_amt);
}

int tickbase::determine_optimal_limit()
{
	if (fast_fire || hide_shot)
		return max_new_cmds;

	return 0;
}

int tickbase::compute_current_limit(int command_number)
{
	if (!command_number)
		return 0;

	const auto& p = prediction::get_pred_info(interfaces::client_state()->last_command_ack);
	auto limit = p.sequence == interfaces::client_state()->last_command_ack ? p.tickbase.limit : 0;

	for (auto i = interfaces::client_state()->last_command_ack + 1; i <= command_number; i++)
	{
		auto& p2 = prediction::get_pred_info(i);
		if (p2.sequence != i)
			continue;

		p2.tickbase.limit = clamp(limit + p2.tickbase.invalid_commands, 0, sv_maxusrcmdprocessticks);
		p2.tickbase.limit = limit = std::max(p2.tickbase.limit - p2.tickbase.extra_commands, 0);
	}

	return limit;
}

float tickbase::get_adjusted_time()
{
	return ticks_to_time(local_player->get_tickbase() - 1);
}

bool tickbase::is_ready()
{
	return !to_recharge && !to_shift;
}