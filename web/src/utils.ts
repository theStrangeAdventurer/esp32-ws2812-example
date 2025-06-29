export async function apiCall(endpoint: string, method = 'GET', data: any = null) {
	try {
		const options: RequestInit = { method, headers: { 'Content-Type': 'application/json' } };
		if (data) options.body = JSON.stringify(data);
		const response = await fetch('/api/' + endpoint, options);
		return response.json();
	} catch (error) {
		console.error('API call failed:', error);
		return { error: error instanceof Error ? error.message : 'Unknown error' };
	}
}

export async function setEffect(effectName: string) {
	await apiCall('effect', 'POST', { effect: effectName });
}

export async function nextEffect() {
	await apiCall('effect/next', 'POST');
}

export function setBrightness(value: string | number) {
	return apiCall('brightness', 'POST', { brightness: parseInt(value as string) });
}

export async function adjustBrightness(delta: number) {
	await apiCall('brightness', 'POST', { delta: delta });
}

export async function setPower(state: boolean) {
	await apiCall('power', 'POST', { power: state });
}

export async function getStatus() {
	const status: Partial<{ current_effect: string, brightness: string, is_running: boolean, total_effects: number }> = await apiCall('status');
	return status;
}

export async function getEffects() {
	const { effects }: { effects: string[] } = await apiCall('effects');
	return effects
}
