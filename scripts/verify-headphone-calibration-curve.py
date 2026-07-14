import argparse
import math
import re
import statistics
from pathlib import Path


def parse_pairs(path):
	text = Path(path).read_text(encoding="utf-8", errors="ignore")
	if "GraphicEQ:" in text:
		text = text.split("GraphicEQ:", 1)[1]
		pattern = r"([-+]?\d+(?:[\.,]\d+)?)\s*(?:Hz)?\s*[,;\t ]+\s*([-+]?\d+(?:[\.,]\d+)?)"
	else:
		pattern = r"(?:^|\n)\s*([-+]?\d+(?:[\.,]\d+)?)\s*(?:Hz)?\s*[,;\t ]+\s*([-+]?\d+(?:[\.,]\d+)?)"
	pairs = []
	for freq_text, gain_text in re.findall(pattern, text):
		freq = float(freq_text.replace(",", "."))
		gain = float(gain_text.replace(",", "."))
		if 1.0 <= freq <= 200000.0 and -200.0 < gain < 200.0:
			pairs.append((freq, gain))
	return sorted(pairs)


def interp(curve, freq):
	if freq <= curve[0][0]:
		return curve[0][1]
	for (f0, g0), (f1, g1) in zip(curve, curve[1:]):
		if freq <= f1:
			return g0 + (g1 - g0) * math.log(freq / f0) / math.log(f1 / f0)
	return curve[-1][1]


def smooth_1_12(curve):
	half_window = 0.5 / 12.0
	result = []
	for freq, gain in curve:
		total = 0.0
		weight_sum = 0.0
		for other_freq, other_gain in curve:
			distance = abs(math.log2(other_freq / freq))
			if distance > half_window:
				continue
			weight = 1.0 - distance / half_window
			total += other_gain * weight
			weight_sum += weight
		result.append((freq, total / weight_sum if weight_sum else gain))
	return result


def robust_online_level(curve):
	gains = sorted(gain for freq, gain in curve if 100.0 <= freq <= 10000.0)
	if not gains:
		return curve
	position = (len(gains) - 1) * 0.97
	low = int(math.floor(position))
	high = min(low + 1, len(gains) - 1)
	fraction = position - low
	level = gains[low] * (1.0 - fraction) + gains[high] * fraction
	return [(freq, max(-24.0, min(0.0, gain - level))) for freq, gain in curve]


def make_correction(left, right, target, ref_hz):
	measurement = [(freq, (gain + interp(right, freq)) * 0.5) for freq, gain in left]
	source = []
	target_grid = []
	freq = 20.0
	while freq <= 15000.0:
		source.append((freq, interp(measurement, freq)))
		target_grid.append((freq, interp(target, freq)))
		freq *= 2 ** (1.0 / 48.0)
	source = smooth_1_12(source)
	target_grid = smooth_1_12(target_grid)
	deltas = [
		source_gain - target_gain
		for (freq, source_gain), (_, target_gain) in zip(source, target_grid)
		if abs(freq - ref_hz) < ref_hz * 0.025
	]
	offset = statistics.median(deltas) if deltas else 0.0
	return robust_online_level([
		(freq, target_gain + offset - source_gain)
		for (freq, source_gain), (_, target_gain) in zip(source, target_grid)
	])


def compare(curve, reference):
	diffs = [abs(interp(curve, freq) - gain) for freq, gain in reference if 20.0 <= freq <= 15000.0]
	signed = [interp(curve, freq) - gain for freq, gain in reference if 20.0 <= freq <= 15000.0]
	return sum(diffs) / len(diffs), max(diffs), sum(signed) / len(signed)


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--left", required=True)
	parser.add_argument("--right", required=True)
	parser.add_argument("--target", required=True)
	parser.add_argument("--reference", action="append", default=[])
	parser.add_argument("--ref-hz", type=float, default=500.0)
	args = parser.parse_args()

	curve = make_correction(parse_pairs(args.left), parse_pairs(args.right), parse_pairs(args.target), args.ref_hz)
	print(f"curve peak={max(gain for _, gain in curve):.3f} dB points={len(curve)}")
	for path in args.reference:
		mae, max_error, bias = compare(curve, parse_pairs(path))
		print(f"{path}: MAE={mae:.3f} dB max={max_error:.3f} dB bias={bias:.3f} dB")


if __name__ == "__main__":
	main()
