#include <stdio.h>
#include <string.h>
#include <math.h>

#include <bdd.h>
#include <expat.h>

#include "pickaxe.h"
#include "transition_system.h"
#include "ltl.h"

extern int yyparse(void);
extern void init_yy(void);
LTLExpr *parsed;

static char *my_true = "true";

char **current_phs;

static BDD check_u(TransitionSystem *model, BDD l, BDD r) {

	BDD res = bdd_addref(r);
	BDD tmp = bdd_addref(pre_exists(model, res)); //get predecessors
	BDD tmp2, tmp3, tmp4;
	while (tmp != 0)
	{
		BDD tmp2 = bdd_addref(bdd_and(l, tmp)); //filter out states that are not included in l
		BDD tmp3 = bdd_addref(bdd_or(res, tmp2)); //append result
		bdd_delref(tmp2);

		bdd_delref(res);
		res = bdd_addref(tmp3);
		bdd_delref(tmp3);

		tmp4 = bdd_addref(pre_exists(model, tmp)); //get predecessors
		bdd_delref(tmp);
		tmp = bdd_addref(tmp4);
		bdd_delref(tmp4);

	}
	bdd_delref(tmp);

	return res;
}

static BDD check_ltl(TransitionSystem *model, LTLExpr *ltl)
{
	BDD res = 0;
	BDD tmp;
	BDD tmp2;
	BDD tmp3;
	BDD tmp4;
	Labels *l;
	switch(ltl->op)
	{
	case ltl_not:
		tmp = check_ltl(model, ltl->expr1);
		res = bdd_addref(bdd_apply(model->all_states, tmp, bddop_diff));
		bdd_delref(tmp);
		break;
	case ltl_and:
		tmp = check_ltl(model, ltl->expr1);
		tmp2 = check_ltl(model, ltl->expr2);
		res = bdd_addref(bdd_and(tmp, tmp2));
		bdd_delref(tmp);
		bdd_delref(tmp2);
		break;
	case ltl_or:
		tmp = check_ltl(model, ltl->expr1);
		tmp2 = check_ltl(model, ltl->expr2);
		res = bdd_addref(bdd_or(tmp, tmp2));
		bdd_delref(tmp);
		bdd_delref(tmp2);
		break;
	case ltl_imp:
	{
		//a -> b is !a | b
		LTLExpr anot; anot.op = ltl_not; anot.expr1 = ltl->expr1;
		LTLExpr or; or.op = ltl_or; or.expr1 = &anot; or.expr2 = ltl->expr2;
		res = check_ltl(model, &or);
		break;
	}
	case ltl_iff:
	{
		// a <-> b is (!a | b) & (a | !b)
		LTLExpr anot2 = {.op = ltl_not, .expr1 = ltl->expr1};
		LTLExpr bnot; bnot.op = ltl_not; bnot.expr1 = ltl->expr2;
		LTLExpr or1; or1.op = ltl_or; or1.expr1 = &anot2; or1.expr2 = ltl->expr2;
		LTLExpr or2; or2.op = ltl_or; or2.expr1 = &bnot; or2.expr2 = ltl->expr1;
		LTLExpr and; and.op = ltl_and; and.expr1 = &or1, and.expr2 = &or2;
		res = check_ltl(model, &and);
		break;
	}
	case ltl_x:
		tmp = check_ltl(model, ltl->expr1);
		res = bdd_addref(pre_exists(model, tmp));
		bdd_delref(tmp);
		break;
	case ltl_f:
		//check inner expression
		res = check_ltl(model, ltl->expr1);
		//get predecessors
		tmp = bdd_addref(pre_exists(model, res));
		while (tmp != 0) //while there are no predecessors
		{
			tmp2 = bdd_addref(bdd_or(res, tmp));
			bdd_delref(res);
			res = bdd_addref(tmp2);
			bdd_delref(tmp2);

			tmp3 = bdd_addref(pre_exists(model, tmp));
			bdd_delref(tmp);
			tmp = bdd_addref(tmp3);
			bdd_delref(tmp3);
		}
		bdd_delref(tmp);

		break;
	case ltl_g:
		tmp3 = check_ltl(model, ltl->expr1);
		tmp = bdd_addref(pre_exists(model, model->pseudo_end)); // all final states
		tmp2 = bdd_addref(bdd_and(tmp, tmp3)); //filter result
		bdd_delref(tmp);
		tmp = bdd_addref(tmp2); // store last predecessors

		res = bdd_addref(tmp2); // put the filtered states to result

		while (tmp != 0) {
			bdd_delref(tmp);
			tmp = bdd_addref(pre_exists(model, tmp2)); // predecessors
			bdd_delref(tmp2);
			tmp2 = bdd_addref(bdd_and(tmp, tmp3)); //filter result
			bdd_delref(tmp);
			tmp = bdd_addref(tmp2); // store last predecessors

			tmp4 = bdd_addref(bdd_or(res, tmp)); // add the filtered states to result
			bdd_delref(res);
			res = bdd_addref(tmp4);
			bdd_delref(tmp4);
		}
		bdd_delref(tmp);
		bdd_delref(tmp2);
		bdd_delref(tmp3);
		break;
	case ltl_u:
		tmp = check_ltl(model, ltl->expr1);
		tmp2 = check_ltl(model, ltl->expr2);
		res = check_u(model, tmp, tmp2);
		bdd_delref(tmp);
		bdd_delref(tmp2);
		break;
	case ltl_const:
		if (strcmp(ltl->name, my_true) == 0)
			res = bdd_addref(model->all_states);
		else
			res = bdd_addref(0);
		printf("constant: %s\n", ltl->name);
		break;
	case ltl_atomic:
	{
		l = get_labels_for(model, ltl->name);  //can create new labels if not exists
		if (l->states_size > 0)
			res = bdd_addref(l->states_bdd);
		else
			res = bdd_addref(0);
		break;
	}
	case ltl_ph:
	{
		LTLExpr *atomic = create_atomic(current_phs[ltl->ph_i]);
		ltl->ph_val = current_phs[ltl->ph_i]; //needed only for printing
		res = check_ltl(model, atomic);
		free(atomic);
		break;
	}
	default:
		printf("ERROR: unknown operator");
	}

	return res;
}

void check_query(TransitionSystem *model, LTLRoot *ltl)
{

	int numphs = ltl->numphs;
	if (numphs == 0)
	{
		//TODO: remove duplicate code
		BDD res = check_ltl(model, ltl->expr);
		double s = support(model, res);
		bdd_delref(res);
		printf("%.2f\t", s);
		to_string(ltl->expr, stdout, 1);
		printf("\n");
		return;
	}

	char *cphs[numphs];
	current_phs = cphs;
	int ph_indexes[numphs];
	for (int i = 0; i < numphs; i++) ph_indexes[i] = 0; //set all indexes to 0

	//calculate the number of loops
	int numloops = 1;
	for (int i = 0; i < numphs; i++)
	{
		if (NULL == ltl->ph_vals[i])
		{
			ltl->ph_vals[i] = new_array();
			for (int j = 0; j < model->activities_size; j++)
			{
				add_element(ltl->ph_vals[i], model->labels[j]->activity);
			}
		}
		numloops *= ltl->ph_vals[i]->length;
	}

	for (int i = 0; i < numloops; i++)
	{

		//make assignments
		for (int n = 0; n < numphs; n++)
		{
			int li = ph_indexes[n];
			VarArray *a = ltl->ph_vals[n];
			current_phs[n] = a->elements[li];
//			printf("current_phs[%d] = %s\n", n, a->elements[li]);
			//increment index
			li = (li + 1) % ltl->ph_vals[n]->length;
			ph_indexes[n] = li;

			//when index makes a full round, increment next. otherwise no need to increment other(s)
			if (li != 0 && i != 0) {
				break;
			}
		}
		//check for duplicate assignments
		for (int n = 0; n < numphs - 1; n++)
			for (int m = n + 1; m < numphs; m++)
				if (ph_indexes[n] == ph_indexes[m])
					goto end;

		BDD res = check_ltl(model, ltl->expr);
		double s = support(model, res);
		bdd_delref(res);
//		printf("%.2f\t", s);
		to_string(ltl->expr, stdout, 1);
		printf("\n");

		end:
		continue;
	}
}


static int event_i = 0;
static char *trace[2000];
static char *last_event;
static char *last_lc = NULL;
static char event[200];

static TransitionSystem *model;

static int in_event = 0;
static void start_element(void *user_data, const char *name, const char **atts)
{
  if (strcmp("trace", name) == 0)
  {
	  event_i = 0;
	  in_event = 0;
  }
  else if (strcmp("event", name) == 0)
  {
	  //printf("event start\n");
	  in_event = 1;
  }
  else if (in_event == 1 && strcmp("string", name) == 0)
  {
	  if(strcmp("concept:name", atts[1]) == 0)
	  {
		  last_event = strdup(atts[3]);
	  }
	  else if (strcmp("lifecycle:transition", atts[1]) == 0)
	  {
		  last_lc = strdup(atts[3]);
	  }
  }
}

static void end_element(void *user_data, const char *name)
{
  if (strcmp("trace", name) == 0)
  {
	  trace[event_i] = NULL;
//	  printf("adding trace %d\n", event_i);
	  add_trace(model, trace);
//	  printf("added\n");
  }
  else if (strcmp("event", name) == 0)
  {

	  strcpy(event, last_event);
	  if (last_lc != NULL)
	  {
		  strcat(event, "#");
		  strcat(event, last_lc);
		  free(last_lc);
		  last_lc = NULL;
	  }
	  free(last_event);
	  trace[event_i] = strdup(event);
	  event_i++;
  }
}


static void parse_log(const char *log_file)
{
	FILE *xes = fopen(log_file, "r");
	if (!xes)
	{
		printf("File not found: %s\n", log_file);
		exit(1);
	}
	char buf[BUFSIZ];
	XML_Parser parser = XML_ParserCreate(NULL);
	int done;
	XML_SetElementHandler(parser, start_element, end_element);
	do {
		int len = (int)fread(buf, 1, sizeof(buf), xes);
		done = len < sizeof(buf);
		if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
			fprintf(stderr,
			"%s at line %lu\n",
			XML_ErrorString(XML_GetErrorCode(parser)),
			XML_GetCurrentLineNumber(parser));
			return;
		}
	} while (!done);
	XML_ParserFree(parser);
}

#include <time.h>
#include <sys/time.h>

//#define _DEBUG 1
int main(int argc, char **args)
{

	struct timeval start, end;
	long delta = 0;
	model = create_emtpty_model();
#ifndef _DEBUG
	char * log_file;
	if (argc < 2)
	{
		printf("No log file name given, using ./log.xes\n");
		log_file = "D:\\test_logs\\var_max_trace_size\\a_test_traces_100_alphabet_10_maxeventstraces_5.xes";
	}
	else
	{
		log_file = args[1];
	}
	parse_log(log_file);
#else

	char *trace1[] = {"C", "A", NULL};
	char *trace2[] = {"D", "A", "B", "A", NULL};
	char *trace3[] = {"A", "B", "B", "D", NULL};
	char *trace4[] = {"G", "F", NULL};
	char *trace5[] = {"A", "A", "B", NULL};

	add_trace(model, trace1);
	add_trace(model, trace2);
	add_trace(model, trace3);
	add_trace(model, trace4);
	add_trace(model, trace5);

	create_bdds(model);
//	BDD pres = pre_all(model, model->pseudo_end);
//	BDD pres = bdd_apply(model->all_states, model->states_bdds[2]));
//	BDD expected = bdd_or(model->states_bdds[1], model->states_bdds[4]);
//	printf("%d, %d", pres, model->states_bdds[2]);
//	print_model(model);
	parsed = create_ltl(create_placeholder("?x"), create_atomic("A"), ltl_f);
	LTLRoot *d_ltl = create_root(parsed);
	init_placeholders(d_ltl, parsed);
//	VarArray *x_vals = new_array();
//	add_element(x_vals, "A");
//	add_element(x_vals, "B");

//	ltl->ph_vals[0] = x_vals;

	check_query(model, d_ltl);
	bdd_gbc();
//	printf("%d", model->states_size);
//	print_model(model);

#endif
#ifndef _DEBUG
	create_bdds(model);
/***************FILTER*********************/
//	VarArray *x_vals0 = new_array();
//	add_element(x_vals0, "ureum#complete");
//	add_element(x_vals0, "albumine#complete");
//	add_element(x_vals0, "creatinine#complete");
//	add_element(x_vals0, "O_DECLINED#complete");
//	add_element(x_vals0, "A_FINALIZED#complete");
//	add_element(x_vals0, "O_SENT#complete");
//	add_element(x_vals0, "O_DECLINED#complete");

//	VarArray *x_vals1 = new_array();
//	add_element(x_vals1, "calcium#complete");
//	add_element(x_vals1, "glucose#complete");
//	add_element(x_vals1, "natrium vlamfotometrisch#complete");
//	add_element(x_vals1, "kalium potentiometrisch#complete");
//	add_element(x_vals1, "thorax#complete");
//	add_element(x_vals1, "O_SENT#complete");
//	add_element(x_vals1, "O_DECLINED#complete");
//
//	VarArray *x_vals2 = new_array();
//	add_element(x_vals2, "A_SUBMITTED#complete");
//	add_element(x_vals2, "W_Completeren aanvraag#schedule");
//	add_element(x_vals2, "A_DECLINED#complete");
//	add_element(x_vals2, "O_DECLINED#complete");
//	add_element(x_vals2, "A_FINALIZED#complete");
//	add_element(x_vals2, "O_SENT#complete");
//	add_element(x_vals2, "O_DECLINED#complete");

/***************FILTER*********************/

	printf("valid\tinvalid\texpression\n");
	init_yy();
	int i = 1;
	while(i != 0)
	{
		i = yyparse();
		if (i == 0 || parsed == NULL)
			break;

		LTLRoot *ltl = create_root(parsed);
		init_placeholders(ltl, parsed);
//		add filter
//		ltl->ph_vals[0] = x_vals0;
//		ltl->ph_vals[1] = x_vals1;
//		ltl->ph_vals[2] = x_vals2;
//		gettimeofday(&start, NULL);
		check_query(model, ltl);
//		gettimeofday(&end, NULL);
//		delta += (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
//		printf("%s\t", log_file);
//		to_string(parsed, stdout, 0);
//		printf("\t%ld\n", delta);
	}

//	printf("%s\t%ld\t%d\n", log_file, delta, model->states_size);
#endif

	return 0;
}
