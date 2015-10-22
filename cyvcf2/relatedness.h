#include <stdint.h>
#include <math.h>

#define HOM_REF 0
#define HET 1
#define HOM_ALT 2
#define UNKNOWN 3

// internal calculate of alternate allele frequency.
inline float aaf(int *gt_types, int32_t n_samples){
	float af = 0;
	int i, n_called = 0;

	for (i = 0; i < n_samples; i++){
		if(gt_types[i] == UNKNOWN){
			continue;
		}
		af += gt_types[i];
		n_called += 1;
	}
	return af / (float)(2 * n_called);
}

int pow2(uint32_t v) {

	int r = 0; // r will be lg(v)
	while (v >>= 1) {
		r++;
	}
	return r;
}

// calculate a bins of IBD between 2 pairs of samples.
// returns 0 starting a new run-length and run_length + 1 if continuing.
// *bins are filled in powers of 2 so that we have a bins of, e.g.:
// (0, 1), (2, 3), (4, 7), (8, 15), (16, 31) ...
// this should be called iteratively, sending the return value in as the new
// run_length.
int ibd(int agt, int bgt, int run_length, float pi, int *bins, int32_t n_bins) {
	if(agt == bgt) {
		if (agt != UNKNOWN) {
			run_length++;
		}
		return run_length;
	}
	// skip unknown.
	if (agt == UNKNOWN || bgt == UNKNOWN) {
		return run_length;
	}

	// if they arent equal genotypes, we only stop the run if they have a lowish relatedness
	float val = (agt - 2 * pi) * (bgt - 2 * pi);
	// end this block.
	if (val < -0.8) {
		int b = pow2(run_length);
        b = (b >= n_bins) ? n_bins : b;
		bins[b]++;
		run_length = 0;
    } else if (val > 0) { // only increment if any info
		run_length += 1;
	}
	return run_length;
}

// related takes an array of genotypes (0=HOM_REF, 1=HET, 2=HOMALT, 3=UNKNOWN) and updates asum and N
// which are used to calculate relatedness between samples j, k as asum[j, k] / N[j, k].
// The result value should be ~1 for self and idential twins, ~0.5 for sibs and parent off-spring
// though that usually seems to be ~0.4 in practice.
// This should be called on few hundred to a few thousand variants that are
// not in linkage and have an aaf > 1 / n_samples (or so).
// asum and N are of length n_samples * n_samples and assumed to be in C order.
int related(int *gt_types, double *asum, int32_t *N, int32_t *ibs0, int32_t *ibs2, int32_t n_samples) {

	int idx, uidx, n_used = 0;
	int32_t j, k;
	float pi = aaf(gt_types, n_samples);
	float numer, val;
	float gtj, gtk;
	float denom = 2.0 * pi * (1.0 - pi);

	for(j=0; j <n_samples; j++){
		// skip unknown
		if(gt_types[j] == UNKNOWN){
			continue;
		}
		gtj = gt_types[j];
		n_used++;
		for(k=j; k<n_samples; k++){
			if(gt_types[k] == UNKNOWN){
				continue;
			}
			uidx = j + k * n_samples;
			idx = j * n_samples + k;
			gtk = gt_types[k];
			if(j != k){
				// multiply by 2 here to get the correct scale. differs from
				// original paper.
				numer = 2.0 * (gtj - 2.0 * pi) * (gtk - 2.0 * pi);
				ibs0[idx] += (gtj != HET && gtk != HET && gtj != gtk);
			} else {
				numer = (gtj * gtj) - (1.0 + 2.0 * pi) * gtj + 2.0 * pi * pi;
				// add 1 for self.
				asum[idx]+=1;
			}
			val = numer / denom;
			// heuristic to avoid too-large values
			//
			if(val > 4.5) {
				val = 4.5;
			} else if (val < -4.5){
				val = -3.5;
				//continue;
			}

			// likely IBD2* of concordant HETs.
			// we don't know the phasing but we just use the prob.
			if (gtj == gtk && gtj != HOM_REF && val > 2.5) {
				// ibs2*
				ibs2[uidx]+=1;
			} else if (val > 2.5) {
				ibs2[idx] += (gtj == gtk && gtk != HET);
			}

			asum[idx] += val;
			N[idx]+= 1;
		}
	}
	return n_used;
}

float r_unphased(int *a_gts, int *b_gts, float f, int32_t n_samples) {
	// http://www.ncbi.nlm.nih.gov/pmc/articles/PMC2710162/pdf/GEN1823839.pdf
	// https://github.com/alanrogers/covld/blob/master/estimate_ld.c
    float vA, vB, vAB, cov, nsqr;
    int suma = 0, sumb = 0, sumaa = 0, sumbb = 0, sumab = 0;
    int i, n=0, a, b;

    for(i=0; i<n_samples; i++) {
        a = a_gts[i];
		if (a == UNKNOWN) continue;
        b = b_gts[i];
		if (b == UNKNOWN) continue;

        n += 1;
        suma += a;
        sumb += b;
        sumaa += a*a;
        sumbb += b*b;
        sumab += a*b;
    }

    nsqr = (double) n*(n-1);
    cov = (n*sumab - suma*sumb)/nsqr;
    vA = (n*sumaa - suma*suma)/nsqr;
    vB = (n*sumbb - sumb*sumb)/nsqr;

    vAB = vA*vB;
    if(vAB > 0) {
        return cov/sqrt(vAB);
    }
	return 0.0;
}