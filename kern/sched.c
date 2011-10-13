#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>


// Choose a user environment to run and run it.
void
sched_yield(void)
{
	// Implement simple round-robin scheduling.
	// Search through 'envs' for a runnable environment,
	// in circular fashion starting after the previously running env,
	// and switch to the first such environment found.
	// It's OK to choose the previously running env if no other env
	// is runnable.
	// But never choose envs[0], the idle environment,
	// unless NOTHING else is runnable.

	// LAB 4: Your code here.
	static int position = 0;	
	cprintf("Entering sched_yield(), position: %d\n", position);
	int start = position, min_niceness = MAX_ENV_NICENESS + 1, claimant_env = 0, look_ahead;
	int i;
	for(position = (position + 1)%NENV; position != start; position = (position+1)%NENV)
	{
		if(position == 0)
			continue;
		if(envs[position].env_status == ENV_RUNNABLE)
		{	
			claimant_env = position;
			min_niceness = envs[position].env_nice;
			// Look ahead to check if we have a more selfish environment here 
			// Doing this so that environments with same priority run in round-robin 
			for(look_ahead = (position + 1)%NENV; look_ahead != position; look_ahead = (look_ahead+1)%NENV)
				if(look_ahead != 0 && envs[look_ahead].env_status == ENV_RUNNABLE)
					if(envs[look_ahead].env_nice < min_niceness)
					{
					 	claimant_env = look_ahead;
						min_niceness = envs[look_ahead].env_nice;
					}

			cprintf("\t\t\t\t\tsched_yield will now run : %x, with niceness: %d\n", claimant_env, min_niceness);
			env_run(&envs[claimant_env]);
		}
	}
	if(!claimant_env && envs[position].env_status == ENV_RUNNABLE)
	{
		cprintf("\t\t\t\t\tsched_yield will now run : %x, with niceness: %d\n", position, envs[position].env_nice);
		env_run(&envs[position]);
	}
	

	// Run the special idle environment when nothing else is runnable.
	if (envs[0].env_status == ENV_RUNNABLE)
		env_run(&envs[0]);
	else {
		cprintf("Destroyed all environments - nothing more to do!\n");
		while (1)
			monitor(NULL);
	}
}
