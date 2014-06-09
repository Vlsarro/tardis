#include "cmontecarlo.h"

npy_int64 line_search(npy_float64 *nu, npy_float64 nu_insert, npy_int64 number_of_lines)
{
  npy_int64 imin, imax;
  imin = 0;
  imax = number_of_lines - 1;
  if (nu_insert > nu[imin])
    {
      return imin;
    }
  else if (nu_insert < nu[imax])
    {
      return imax + 1;
    }
  else
    {
      return binary_search(nu, nu_insert, imin, imax) + 1;
    }
}

npy_int64 binary_search(npy_float64 *x, npy_float64 x_insert, npy_int64 imin, npy_int64 imax)
{
  if (x_insert > x[imin] || x_insert < x[imax])
    {
      PyErr_SetString(PyExc_ValueError, "Binary Search called but not inside domain. Abort!");
      return -1;
    }
  int imid;
  while (imax - imin > 2)
    {
      imid = (imin + imax) / 2;
      if (x[imid] < x_insert)
	{
	  imax = imid + 1;
	}
      else
	{
	  imin = imid;
	}
    }
  if (imax - imid == 2)
    {
      if (x_insert < x[imin + 1])
	{
	  return imin + 1;
	}
    }
  return imin;
}

npy_float64 compute_distance2outer(npy_float64 r, npy_float64 mu, npy_float64 r_outer)
{
  return sqrt(r_outer * r_outer + ((mu * mu - 1.0) * r * r)) - (r * mu);
}

npy_float64 compute_distance2inner(npy_float64 r, npy_float64 mu, npy_float64 r_inner)
{
  npy_float64 check = r_inner * r_inner + (r * r * (mu * mu - 1.0));
  if (check < 0.0) 
    {
      return MISS_DISTANCE;
    }
  else
    {
      return mu < 0.0 ? -r * mu - sqrt(check) : MISS_DISTANCE;
    }
}

npy_float64 compute_distance2line(npy_float64 r, npy_float64 mu, npy_float64 nu, npy_float64 nu_line, npy_float64 t_exp, npy_float64 inverse_t_exp, npy_float64 last_line, npy_float64 next_line, npy_int64 cur_zone_id)
{
  npy_float64 comov_nu, doppler_factor;
  doppler_factor = 1.0 - mu * r * inverse_t_exp * INVERSE_C;
  comov_nu = nu * doppler_factor;
  if (comov_nu < nu_line)
    {
      fprintf(stderr, "ERROR: Comoving nu less than nu_line!\n");
      fprintf(stderr, "comov_nu = %f\n", comov_nu);
      fprintf(stderr, "nu_line = %f\n", nu_line);
      fprintf(stderr, "(comov_nu - nu_line) / nu_line = %f\n", (comov_nu - nu_line) / nu_line);
      fprintf(stderr, "last_line = %f\n", last_line);
      fprintf(stderr, "next_line = %f\n", next_line);
      fprintf(stderr, "r = %f\n", r);
      fprintf(stderr, "mu = %f\n", mu);
      fprintf(stderr, "nu = %f\n", nu);
      fprintf(stderr, "doppler_factor = %f\n", doppler_factor);
      fprintf(stderr, "cur_zone_id = %d\n", cur_zone_id);
      PyErr_SetString(PyExc_RuntimeError, "comov_nu less than nu_line");
      return 0;
    }
  return ((comov_nu - nu_line) / nu) * C * t_exp;
}

npy_float64 compute_distance2electron(npy_float64 r, npy_float64 mu, npy_float64 tau_event, npy_float64 inverse_ne)
{
  return tau_event * inverse_ne;
}

inline npy_int64 macro_atom(npy_int64 activate_level, npy_float64 *p_transition, npy_int64 p_transition_nd, npy_int64 *type_transition, npy_int64 *target_level_id, npy_int64 *target_line_id, npy_int64 *unroll_reference, npy_int64 cur_zone_id)
{
  npy_int64 emit, i = 0;
  npy_float64 p, event_random = 0.0;
  while (1)
    {
      event_random = rk_double(&mt_state);
      i = unroll_reference[activate_level];
      p = 0.0;
      while (1)
	{
	  p = p + p_transition[cur_zone_id * p_transition_nd + i];
	  if (p > event_random)
	    {
	      emit = type_transition[i];
	      activate_level = target_level_id[i];
	      break;
	    }
	  i += 1;
	}
      if (emit == -1)
	{
	  return target_line_id[i];
	}
    }
}

inline npy_float64 move_packet(npy_float64 *r, npy_float64 *mu, npy_float64 nu, npy_float64 energy, npy_float64 distance, npy_float64 *js, npy_float64 *nubars, npy_float64 inverse_t_exp, npy_int64 cur_zone_id, npy_int64 virtual_packet)
{
  npy_float64 new_r, doppler_factor, comov_energy, comov_nu;
  doppler_factor = 1.0 - *mu * (*r) * inverse_t_exp * INVERSE_C;
  if (distance <= 0.0)
    {
      return doppler_factor;
    }
  new_r = sqrt((*r) * (*r) + distance * distance + 2.0 * (*r) * distance * (*mu));
  *mu = (*mu * (*r) + distance) / new_r;
  *r = new_r;
  if (virtual_packet > 0)
    {
      return doppler_factor;
    }
  comov_energy = energy * doppler_factor;
  comov_nu = nu * doppler_factor;
  js[cur_zone_id] += comov_energy * distance;
  nubars[cur_zone_id] += comov_energy * distance * comov_nu;
  return doppler_factor;
}

inline void increment_j_blue_estimator(npy_int64 *current_line_id, npy_float64 *current_nu, npy_float64 *current_energy, npy_float64 *mu, npy_float64 *r, npy_float64 d_line, npy_int64 j_blue_idx, npy_float64 inverse_time_explosion, npy_float64 *line_lists_j_blues)
{
  npy_float64 comov_energy, comov_nu, r_interaction, mu_interaction, distance, doppler_factor;
  distance = d_line;
  r_interaction = sqrt((*r) * (*r) + distance * distance + 2 * (*r) * distance * (*mu));
  mu_interaction = (*mu * (*r) + distance) / r_interaction;
  doppler_factor = 1.0 - mu_interaction * r_interaction * inverse_time_explosion * INVERSE_C;
  comov_energy = *current_energy * doppler_factor;
  comov_nu = *current_nu * doppler_factor;
  line_lists_j_blues[j_blue_idx] += comov_energy / *current_nu;
}

void init_storage_model(storage_model_t *storage, 
			npy_float64 *packet_nus,
			npy_float64 *packet_mus,
			npy_float64 *packet_energies,
			npy_float64 *r_inner,
			npy_float64 *line_list_nu,
			npy_float64 *output_nus,
			npy_float64 *output_energies,
			npy_float64 time_explosion,
			npy_float64 spectrum_start_nu,
			npy_float64 spectrum_end_nu,
			npy_float64 spectrum_delta_nu,
			npy_float64 *spectrum_virt_nu,
			npy_float64 sigma_thomson,
			npy_float64 *electron_densities,
			npy_float64 *inverse_electron_densities,
			npy_float64 *js,
			npy_float64 *nubars,
			npy_int64 no_of_lines,
			npy_int64 no_of_packets)
{
  storage->packet_nus = packet_nus;
  storage->packet_mus = packet_mus;
  storage->packet_energies = packet_energies;
  storage->r_inner = r_inner;
  storage->output_nus = output_nus;
  storage->output_energies = output_energies;
  storage->time_explosion = time_explosion;
  storage->inverse_time_explosion = 1.0 / time_explosion;
  storage->spectrum_start_nu = spectrum_start_nu;
  storage->spectrum_end_nu = spectrum_end_nu;
  storage->spectrum_delta_nu = spectrum_delta_nu;
  storage->spectrum_virt_nu = spectrum_virt_nu;
  storage->sigma_thomson = sigma_thomson;
  storage->inverse_sigma_thomson = 1.0 / sigma_thomson;
  storage->electron_densities = electron_densities;
  storage->inverse_electron_densities = inverse_electron_densities;
  storage->js = js;
  storage->nubars = nubars;
  storage->no_of_lines = no_of_lines;
  storage->no_of_packets = no_of_packets;
  storage->current_packet_id = -1;
}

npy_int64 montecarlo_one_packet(storage_model_t *storage, npy_float64 *current_nu, npy_float64 *current_energy, npy_float64 *current_mu, npy_int64 *current_shell_id, npy_float64 *current_r, npy_int64 *current_line_id, npy_int64 *last_line, npy_int64 *close_line, npy_int64 *recently_crossed_boundary, npy_int64 virtual_packet_flag, npy_int64 virtual_mode)
{
  npy_int64 i;
  npy_float64 current_nu_virt;
  npy_float64 current_energy_virt;
  npy_float64 current_mu_virt;
  npy_int64 current_shell_id_virt;
  npy_float64 current_r_virt;
  npy_int64 current_line_id_virt;
  npy_int64 last_line_virt;
  npy_int64 close_line_virt;
  npy_int64 recently_crossed_boundary_virt;
  npy_float64 mu_bin;
  npy_float64 mu_min;
  npy_float64 doppler_factor_ratio;
  npy_float64 weight;
  npy_int64 reabsorbed;
  npy_int64 virt_id_nu;
  if (virtual_mode == 0)
    {
      reabsorbed = montecarlo_one_packet_loop(storage, current_nu, current_energy, current_mu, current_shell_id, current_r, current_line_id, last_line, close_line, recently_crossed_boundary, virtual_packet_flag, 0);
    }
  else
    {
      for (i = 0; i < virtual_packet_flag; i++)
	{
	  recently_crossed_boundary_virt = *recently_crossed_boundary;
	  close_line_virt = *close_line;
	  last_line_virt = *last_line;
	  current_line_id_virt = *current_line_id;
	  current_r_virt = *current_r;
	  current_shell_id_virt = *current_shell_id;
	  if (current_r_virt > storage->r_inner[0])
	    {
	      mu_min = -1.0 * sqrt(1.0 - (storage->r_inner[0] / current_r_virt) * (storage->r_inner[0] / current_r_virt));
	    }
	  else
	    {
	      mu_min = 0.0;
	    }
	  mu_bin = (1.0 - mu_min) / virtual_packet_flag;
	  current_mu_virt = mu_min + (i + rk_double(&mt_state)) * mu_bin;
	  if (virtual_mode == -2)
	    {
	      weight = 1.0 / virtual_packet_flag;
	    }
	  else if (virtual_mode == -1)
	    {
	      weight = 2.0 * current_mu_virt / virtual_packet_flag;
	    }
	  else if (virtual_mode == 1)
	    {
	      weight = (1.0 - mu_min) / 2.0 / virtual_packet_flag;
	    }
	  else
	    {
	      // Something has gone horribly wrong!
	    }
	  doppler_factor_ratio = (1.0 - *current_mu * (*current_r) * storage->inverse_time_explosion * INVERSE_C) / (1.0 - current_mu_virt * current_r_virt * storage->inverse_time_explosion * INVERSE_C);
	  current_energy_virt = *current_energy * doppler_factor_ratio;
	  current_nu_virt = *current_nu * doppler_factor_ratio;
	  reabsorbed = montecarlo_one_packet_loop(storage, &current_nu_virt, &current_energy_virt, &current_mu_virt, &current_shell_id_virt, &current_r_virt, &current_line_id_virt, &last_line_virt, &close_line_virt, &recently_crossed_boundary_virt, virtual_packet_flag, 1);
	  if ((current_nu_virt < storage->spectrum_end_nu) && (current_nu_virt > storage->spectrum_start_nu))
	    {
	      virt_id_nu = floor((current_nu_virt - storage->spectrum_start_nu) / storage->spectrum_delta_nu);
	      storage->spectrum_virt_nu[virt_id_nu] += current_energy_virt * weight;
	    }
	}
    }
}

npy_int64 montecarlo_one_packet_loop(storage_model_t *storage, npy_float64 *current_nu, npy_float64 *current_energy, npy_float64 *current_mu, npy_int64 *current_shell_id, npy_float64 *current_r, npy_int64 *current_line_id, npy_int64 *last_line, npy_int64 *close_line, npy_int64 *recently_crossed_boundary, npy_int64 virtual_packet_flag, npy_int64 virtual_packet)
{
  npy_float64 nu_electron = 0.0;
  npy_float64 comov_nu = 0.0;
  npy_float64 comov_energy = 0.0;
  npy_float64 energy_electron = 0.0;
  npy_int64 emission_line_id = 0;
  npy_int64 activate_level_id = 0;
  npy_float64 doppler_factor = 0.0;
  npy_float64 old_doppler_factor = 0.0;
  npy_float64 inverse_doppler_factor = 0.0;
  npy_float64 tau_line = 0.0;
  npy_float64 tau_electron = 0.0;
  npy_float64 tau_combined = 0.0;
  npy_float64 tau_event = 0.0;
  npy_float64 d_inner = 0.0;
  npy_float64 d_outer = 0.0;
  npy_float64 d_line = 0.0;
  npy_float64 d_electron = 0.0;
  npy_int64 reabsorbed = 0;
  npy_float64 nu_line = 0.0;
  npy_int64 virtual_close_line = 0;
  npy_int64 j_blue_idx = -1;
  // Initializing tau_event if it's a real packet.
  if (virtual_packet == 0)
    {
      tau_event = -log(rk_double(&mt_state));
    }
  // For a virtual packet tau_event is the sum of all the tau's that the packet passes.
  while (1)
    {
      // Check if we are at the end of line list.
      if (*last_line == 0)
	{
	  nu_line = storage->line_list_nu[*current_line_id];
	}
      // Check if the last line was the same nu as the current line.
      if (*close_line == 1)
	{
	  // If so set the distance to the line to 0.0
	  d_line = 0.0;
	  // Reset close_line.
	  *close_line = 0;
	}
      else
	{
	  if (*recently_crossed_boundary == 1)
	    {
	      d_inner = MISS_DISTANCE;
	    }
	  else
	    {
	      d_inner = compute_distance2inner(*current_r, *current_mu, storage->r_inner[*current_shell_id]);
	    }
	  d_outer = compute_distance2outer(*current_r, *current_mu, storage->r_outer[*current_shell_id]);
	  if (*last_line == 1)
	    {
	      d_line = MISS_DISTANCE;
	    }
	  else
	    {
	      d_line = compute_distance2line(*current_r, *current_mu, *current_nu, nu_line, storage->time_explosion, storage->inverse_time_explosion, storage->line_list_nu[*current_line_id - 1], storage->line_list_nu[*current_line_id + 1], *current_shell_id);
	    }
	  if (virtual_packet > 0)
	    {
	      d_electron = MISS_DISTANCE;
	    }
	  else
	    {
	      d_electron = compute_distance2electron(*current_r, *current_mu, tau_event, storage->inverse_electron_densities[*current_shell_id] * storage->inverse_sigma_thomson);
	    }
	}
      // Propagating outwards.
      if ((d_outer <= d_inner) && (d_outer <= d_electron) && (d_outer < d_line))
	{
	  move_packet(current_r, current_mu, *current_nu, *current_energy, d_outer, storage->js, storage->nubars, storage->inverse_time_explosion, *current_shell_id, virtual_packet);
	  if (virtual_packet > 0)
	    {
	      tau_event += d_outer * storage->electron_densities[*current_shell_id] * storage->sigma_thomson;
	    }
	  else
	    {
	      tau_event = -log(rk_double(&mt_state));
	    }
	  if (*current_shell_id < storage->no_of_shells - 1)
	    {
	      *current_shell_id += 1;
	      *recently_crossed_boundary = 1;
	    }
	  else
	    {
	      reabsorbed = 0;
	      break;
	    }
	}
      // Propagating inwards.
      else if ((d_inner <= d_electron) && (d_inner < d_line))
	{
	  move_packet(current_r, current_mu, *current_nu, *current_energy, d_inner, storage->js, storage->nubars, storage->inverse_time_explosion, *current_shell_id, virtual_packet);
	  if (virtual_packet > 0)
	    {
	      tau_event += d_inner * storage->electron_densities[*current_shell_id] * storage->sigma_thomson;
	    }
	  else
	    {
	      tau_event = -log(rk_double(&mt_state));
	    }
	  if (*current_shell_id > 0)
	    {
	      *current_shell_id -= 1;
	      *recently_crossed_boundary = -1;
	    }
	  else
	    {
	      if ((storage->reflective_inner_boundary == 0) || (rk_double(&mt_state) > storage->inner_boundary_albedo))
		{
		  reabsorbed = 1;
		  break;
		}
	      else
		{
		  doppler_factor = 1.0 - *current_mu * (*current_r) * storage->inverse_time_explosion * INVERSE_C;
		  comov_nu = *current_nu * doppler_factor;
		  comov_energy = *current_energy * doppler_factor;
		  *current_mu = rk_double(&mt_state);
		  inverse_doppler_factor = 1.0 / (1.0 - *current_mu * (*current_r) * storage->inverse_time_explosion * INVERSE_C);
		  *current_nu = comov_nu * inverse_doppler_factor;
		  *current_energy = comov_energy * inverse_doppler_factor;
		  *recently_crossed_boundary = 1;
		  if (virtual_packet_flag > 0)
		    {
		      montecarlo_one_packet(storage, current_nu, current_energy, current_mu, current_shell_id, current_r, current_line_id, last_line, close_line, recently_crossed_boundary, virtual_packet_flag, -2);
		    }
		}
	    }
	}
      else if (d_electron < d_line)
	{
	  doppler_factor = move_packet(current_r, current_mu, *current_nu, *current_energy, d_electron, storage->js, storage->nubars, storage->inverse_time_explosion, *current_shell_id, virtual_packet);
	  comov_nu = *current_nu * doppler_factor;
	  comov_energy = *current_energy * doppler_factor;
	  *current_mu = 2.0 * rk_double(&mt_state) - 1.0;
	  inverse_doppler_factor = 1 / (1.0 - *current_mu * (*current_r) * storage->inverse_time_explosion * INVERSE_C);
	  *current_nu = comov_nu * inverse_doppler_factor;
	  *current_energy = comov_energy * inverse_doppler_factor;
	  tau_event = -log(rk_double(&mt_state));
	  *recently_crossed_boundary = 0;
	  storage->last_interaction_type[storage->current_packet_id] = 1;
	  if (virtual_packet_flag > 0)
	    {
	      montecarlo_one_packet(storage, current_nu, current_energy, current_mu, current_shell_id, current_r, current_line_id, last_line, close_line, recently_crossed_boundary, virtual_packet_flag, 1);
	    }
	}
      // Line scatter event.
      else
	{
	  if (virtual_packet == 0)
	    {
	      j_blue_idx = *current_shell_id * storage->line_lists_j_blues_nd + *current_line_id;
	      increment_j_blue_estimator(current_line_id, current_nu, current_energy, current_mu, current_r, d_line, j_blue_idx, storage->inverse_time_explosion, storage->line_lists_j_blues);
	    }
	  tau_line = storage->line_lists_tau_sobolevs[*current_shell_id * storage->line_lists_tau_sobolevs_nd + *current_line_id];
	  tau_electron = storage->sigma_thomson * storage->electron_densities[*current_shell_id] * d_line;
	  tau_combined = tau_line + tau_electron;
	  *current_line_id += 1;
	  if (*current_line_id == storage->no_of_lines)
	    {
	      *last_line = 1;
	    }
	  if (virtual_packet > 0)
	    {
	      tau_event += tau_line;
	    }
	  else
	    {
	      if (tau_event < tau_combined)
		{
		  old_doppler_factor = move_packet(current_r, current_mu, *current_nu, *current_energy, d_line, storage->js, storage->nubars, storage->inverse_time_explosion, *current_shell_id, virtual_packet);
		  *current_mu = 2.0 * rk_double(&mt_state) - 1.0;
		  inverse_doppler_factor = 1.0 / (1.0 - *current_mu * (*current_r) * storage->inverse_time_explosion * INVERSE_C);
		  comov_nu = *current_nu * old_doppler_factor;
		  comov_energy = *current_energy * old_doppler_factor;
		  *current_energy = comov_energy * inverse_doppler_factor;
		  storage->last_line_interaction_in_id[storage->current_packet_id] = *current_line_id - 1;
		  storage->last_line_interaction_shell_id[storage->current_packet_id] = *current_shell_id;
		  storage->last_interaction_type[storage->current_packet_id] = 2;
		  if (storage->line_interaction_id == 0)
		    {
		      emission_line_id = *current_line_id - 1;
		    }
		  else if (storage->line_interaction_id >= 1)
		    {
		      activate_level_id = storage->line2macro_level_upper[*current_line_id - 1];
		      emission_line_id = macro_atom(activate_level_id, storage->transition_probabilities, storage->transition_probabilities_nd, storage->transition_type, storage->destination_level_id, storage->transition_line_id, storage->macro_block_references, *current_shell_id);
		    }
		  storage->last_line_interaction_out_id[storage->current_packet_id] = emission_line_id;
		  *current_nu = storage->line_list_nu[emission_line_id] * inverse_doppler_factor;
		  nu_line = storage->line_list_nu[emission_line_id];
		  *current_line_id = emission_line_id + 1;
		  tau_event = -log(rk_double(&mt_state));
		  *recently_crossed_boundary = 0;
		  if (virtual_packet_flag > 0)
		    {
		      virtual_close_line = 0;
		      if (*last_line == 0)
			{
			  if (fabs(storage->line_list_nu[*current_line_id] - nu_line) / nu_line < 1e-7)
			    {
			      virtual_close_line = 1;
			    }
			}
		      montecarlo_one_packet(storage, current_nu, current_energy, current_mu, current_shell_id, current_r, current_line_id, last_line, &virtual_close_line, recently_crossed_boundary, virtual_packet_flag, 1);
		      virtual_close_line = 0;
		    }
		}
	      else 
		{
		  tau_event -= tau_line;
		}
	    }
	  if (*last_line == 0)
	    {
	      if (fabs(storage->line_list_nu[*current_line_id] - nu_line) / nu_line < 1e-7)
		{
		  *close_line = 1;
		}
	    }
	}
      if (virtual_packet > 0)
	{
	  if (tau_event > 10.0)
	    {
	      tau_event = 100.0;
	      reabsorbed = 0;
	      break;
	    }
	}
    } 
  if (virtual_packet > 0)
    {
      *current_energy = *current_energy * exp(-1.0 * tau_event);
    }
  return reabsorbed;
}
