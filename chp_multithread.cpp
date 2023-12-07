#include <omp.h>
#include <chrono> 
#include <iostream> 
#include <cstring>
#include "quantum.cpp"
#include "tableau.cpp"

#define         CNOT		0
#define         HADAMARD	1
#define         PHASE		2
#define         MEASURE		3

void cnot(QState *q, long b, long c)
{
	long i;
	long b5;
	long c5;
	unsigned long pwb;
	unsigned long pwc;

	b5 = b>>5;
	c5 = c>>5;
	pwb = q->pw[b&31];
	pwc = q->pw[c&31];
	#pragma omp parallel for
	for (i = 0; i < 2*q->n; i++)
	{
         if (q->x[i][b5]&pwb) q->x[i][c5] ^= pwc;
         if (q->z[i][c5]&pwc) q->z[i][b5] ^= pwb;
		 if ((q->x[i][b5]&pwb) && (q->z[i][c5]&pwc) &&
			 (q->x[i][c5]&pwc) && (q->z[i][b5]&pwb))
				q->r[i] = (q->r[i]+2)%4;
		if ((q->x[i][b5]&pwb) && (q->z[i][c5]&pwc) &&
			!(q->x[i][c5]&pwc) && !(q->z[i][b5]&pwb))
				q->r[i] = (q->r[i]+2)%4;
	}
}

void hadamard(QState *q, long b)
{
	long i;
	unsigned long tmp;
	long b5;
	unsigned long pw;

	b5 = b>>5;
	pw = q->pw[b&31];
	#pragma omp parallel for private(tmp)
	for (i = 0; i < 2*q->n; i++)
	{
         tmp = q->x[i][b5];
         q->x[i][b5] ^= (q->x[i][b5] ^ q->z[i][b5]) & pw;
         q->z[i][b5] ^= (q->z[i][b5] ^ tmp) & pw;
         if ((q->x[i][b5]&pw) && (q->z[i][b5]&pw)) q->r[i] = (q->r[i]&2)%4;
	}
}

void phase(QState *q, long b)
{
	long i;
	long b5;
	unsigned long pw;

	b5 = b>>5;
	pw = q->pw[b&31];
	#pragma omp parallel for
	for (i = 0; i < 2*q->n; i++)
	{
         if ((q->x[i][b5]&pw) && (q->z[i][b5]&pw)) q->r[i] = (q->r[i]+2)%4;
         q->z[i][b5] ^= q->x[i][b5]&pw;
	}
}

void initstate_(QState *q, long n, char *s)
{
    long i;
    long j;

    q->n = n;
    q->x = new unsigned long*[2*q->n + 1];
    q->z = new unsigned long*[2*q->n + 1];
    q->r = new int[2*q->n + 1];
    q->over32 = (q->n>>5) + 1;
    q->pw[0] = 1;
    for (i = 1; i < 32; i++)
        q->pw[i] = 2*q->pw[i-1];
    for (i = 0; i < 2*q->n + 1; i++)
    {
        q->x[i] = new unsigned long[q->over32];
        q->z[i] = new unsigned long[q->over32];
        for (j = 0; j < q->over32; j++)
        {
            q->x[i][j] = 0;
            q->z[i][j] = 0;
        }
        if (i < q->n)
            q->x[i][i>>5] = q->pw[i&31];
        else if (i < 2*q->n)
        {
            j = i-q->n;
            q->z[i][j>>5] = q->pw[j&31];
        }
        q->r[i] = 0;
    }
    //if (s) preparestate(q, s);

    return;
}

void error(int k)

{

	if (k==0) std::cout <<"\nSyntax: chp [-options] <filename> [input]\n";
	if (k==1) std::cout <<"\nFile not found\n";
	exit(0);

}

int measure(QState *q, long b, int sup)
{
    int ran = 0;
    long i;
    long p; // pivot row in stabilizer
    long m; // pivot row in destabilizer
    long b5;
    unsigned long pw;
    bool break_flag = false;

    b5 = b>>5;
    pw = q->pw[b&31];
    for (p = 0; p < q->n; p++)         // loop over stabilizer generators
    {
		if (break_flag) continue;
        if (q->x[p+q->n][b5]&pw) ran = 1;         // if a Zbar does NOT commute with Z_b (the
        if (ran) break_flag = true;  // operator being measured), then outcome is random
    }

    // If outcome is indeterminate
    if (ran)
    {
        rowcopy(q, p, p + q->n);                         // Set Xbar_p := Zbar_p
        rowset(q, p + q->n, b + q->n);                 // Set Zbar_p := Z_b
        q->r[p + q->n] = 2*(rand()%2);                 // moment of quantum randomness
        #pragma omp parallel for
        for (i = 0; i < 2*q->n; i++)                 // Now update the Xbar's and Zbar's that don't commute with
            if ((i!=p) && (q->x[i][b5]&pw))         // Z_b
                rowmult(q, i, p);
        if (q->r[p + q->n]) return 3;
        else return 2;
    }

    // If outcome is determinate
    if ((!ran) && (!sup))
    {
        for (m = 0; m < q->n; m++)                         // Before we were checking if stabilizer generators commute
            if (q->x[m][b5]&pw) break;                 // with Z_b; now we're checking destabilizer generators
        rowcopy(q, 2*q->n, m + q->n);
        #pragma omp parallel for
        for (i = m+1; i < q->n; i++)
            if (q->x[i][b5]&pw)
                rowmult(q, 2*q->n, i + q->n);
        if (q->r[2*q->n]) return 1;
        else return 0;
    }

    return 0;
}

void readprog(QProg *h, char *fn, char *params)
{
	long t;
	char fn2[255];
	FILE *fp;
	char c=0;
	long val;
	long l;

	h->DISPQSTATE = 0;
	h->DISPTIME = 0;
	h->SILENT = 0;
	h->DISPPROG = 0;
	h->SUPPRESSM = 0;
	if (params)
	{
         l = std::strlen(params);
         for (t = 1; t < l; t++)
         {
                 if ((params[t]=='q')||(params[t]=='Q')) h->DISPQSTATE = 1;
                 if ((params[t]=='p')||(params[t]=='P')) h->DISPPROG = 1;
                 if ((params[t]=='t')||(params[t]=='T')) h->DISPTIME = 1;
                 if ((params[t]=='s')||(params[t]=='S')) h->SILENT = 1;
                 if ((params[t]=='m')||(params[t]=='M')) h->SUPPRESSM = 1;
         }
	}

	sprintf(fn2, "%s", fn);
	fp = fopen(fn2, "r");
	if (!fp)
	{
         sprintf(fn2, "%s.chp", fn);
         fp = fopen(fn2, "r");
         if (!fp) error(1);
	}
	while (!feof(fp)&&(c!='#'))
         fscanf(fp, "%c", &c);
	if (c!='#') error(2);
	h->T = 0;
	h->n = 0;
	while (!feof(fp))
	{
         fscanf(fp, "%c", &c);
         if ((c=='\r')||(c=='\n'))
                 continue;
         fscanf(fp, "%ld", &val);
         if (val+1 > h->n) h->n = val+1;
         if ((c=='c')||(c=='C'))
         {
                 fscanf(fp, "%ld", &val);
                 if (val+1 > h->n) h->n = val+1;
         }
         h->T++;
	}
	fclose(fp);
	h->a = new char[h->T];
	h->b = new long[h->T];
	h->c = new long[h->T];
	fp = fopen(fn2, "r");
	while (!feof(fp)&&(c!='#'))
         fscanf(fp, "%c", &c);
	t=0;
	while (!feof(fp))
	{
         fscanf(fp, "%c", &c);
         if ((c=='\r')||(c=='\n'))
                 continue;
         if ((c=='c')||(c=='C')) h->a[t] = CNOT;
         if ((c=='h')||(c=='H')) h->a[t] = HADAMARD;
         if ((c=='p')||(c=='P')) h->a[t] = PHASE;
         if ((c=='m')||(c=='M')) h->a[t] = MEASURE;
         fscanf(fp, "%ld", &h->b[t]);
         if (h->a[t]==CNOT) fscanf(fp, "%ld", &h->c[t]);
         t++;
	}
	fclose(fp);

	return;
}

void runprog(QProg *h, QState *q)
{
	long t;
	int m; // measurement result
	time_t tp;
	double dt;
	char mvirgin = 1;

	time(&tp);
	for (t = 0; t < h->T; t++)
	{
         if (h->a[t]==CNOT) cnot(q,h->b[t],h->c[t]);
         if (h->a[t]==HADAMARD) hadamard(q,h->b[t]);
         if (h->a[t]==PHASE) phase(q,h->b[t]);
         if (h->a[t]==MEASURE)
         {
                 if (mvirgin && h->DISPTIME)
                 {
                         dt = difftime(time(0),tp);
                         printf("\nGate time: %lf seconds", dt);
                         printf("\nTime per 10000 gates: %lf seconds", dt*10000.0f/(h->T - h->n));
                         time(&tp);
                 }
                 mvirgin = 0;
                 m = measure(q,h->b[t],h->SUPPRESSM);
                 if (!h->SILENT)
                 {
                         printf("\nOutcome of measuring qubit %ld: ", h->b[t]);
                         if (m>1) printf("%d (random)", m-2);
                         else printf("%d", m);
                 }
         }
         if (h->DISPPROG)
         {
                 if (h->a[t]==CNOT)         printf("\nCNOT %ld->%ld", h->b[t], h->c[t]);
                 if (h->a[t]==HADAMARD) printf("\nHadamard %ld", h->b[t]);
                 if (h->a[t]==PHASE)         printf("\nPhase %ld", h->b[t]);
         }
	}
	printf("\n");
	if (h->DISPTIME)
	{
         dt = difftime(time(0),tp);
         printf("\nMeasurement time: %lf seconds", dt);
         printf("\nTime per 10000 measurements: %lf seconds\n", dt*10000.0f/h->n);
	}
	return;
}

int main(int argc, char **argv)
{
    QProg *h;
    QState *q;
    int param=0; // whether there are command-line parameters

    std::cout << "\nCHP: Efficient Simulator for Stabilizer Quantum Circuits";
    std::cout << "\nby Scott Aaronson\n";
    srand(time(0));
    if (argc==1) error(0);
    if (argv[1][0]=='-') param = 1;
    h = new QProg;
    q = new QState;
    if (param) 
        readprog(h,argv[2],argv[1]);
    else 
        readprog(h,argv[1],NULL);
    if (argc==(3+param)) 
        initstate_(q,h->n,argv[2+param]);
    else 
        initstate_(q,h->n,NULL);
    runprog(h,q);

    // Don't forget to delete the dynamically allocated memory
    delete h;
    delete q;

    return 0;
}
