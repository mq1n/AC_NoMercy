// command.cpp: implements the parsing and execution of a tiny script language which
// is largely backwards compatible with the quake console language.

#include "cube.h"

bool allowidentaccess(ident *id);
char *exchangestr(char *o, const char *n) { delete[] o; return newstring(n); }
void scripterr();

vector<int> contextstack;
bool contextsealed = false;
bool contextisolated[IEXC_NUM] = { false };
int execcontext;

bool loop_break = false, loop_skip = false;             // break or continue (skip) current loop
int loop_level = 0;                                      // avoid bad calls of break & continue

hashtable<const char *, ident> *idents = NULL;          // contains ALL vars/commands/aliases

bool persistidents = true, neverpersist = false;
COMMANDF(persistidents, ARG_1INT, (int on) {
    persistidents = neverpersist ? false : on;
});

void clearstack(ident &id)
{
    identstack *stack = id.stack;
    while(stack)
    {
        delete[] stack->action;
        identstack *tmp = stack;
        stack = stack->next;
        delete tmp;
    }
    id.stack = NULL;
}

void pushident(ident &id, char *val, int context = execcontext)
{
    if(id.type != ID_ALIAS) return;
    identstack *stack = new identstack;
    stack->action = id.executing==id.action ? newstring(id.action) : id.action;
    stack->context = id.context;
    stack->next = id.stack;
    id.stack = stack;
    id.action = val;
    id.context = context;
}

void popident(ident &id)
{
    if(id.type != ID_ALIAS || !id.stack) return;
    if(id.action != id.executing) delete[] id.action;
    identstack *stack = id.stack;
    id.action = stack->action;
    id.stack = stack->next;
    id.context = stack->context;
    delete stack;
}

ident *newident(const char *name, int context = execcontext)
{
    ident *id = idents->access(name);
    if(!id)
    {
        ident init(ID_ALIAS, newstring(name), newstring(""), persistidents, context);
        id = &idents->access(init.name, init);
    }
    return id;
}

void pusha(const char *name, char *action)
{
    ident *id = newident(name, execcontext);
    if(contextisolated[execcontext] && execcontext > id->context)
    {
        conoutf("cannot redefine alias %s in this execution context", id->name);
        scripterr();
        return;
    }
    pushident(*id, action);
}

void push(const char *name, const char *action)
{
    pusha(name, newstring(action));
}

void pop(const char *name)
{
    ident *id = idents->access(name);
    if(!id) return;
    if(contextisolated[execcontext] && execcontext > id->context)
    {
        conoutf("cannot redefine alias %s in this execution context", id->name);
        scripterr();
        return;
    }
    popident(*id);
}

COMMAND(push, ARG_2STR);
COMMAND(pop, ARG_1STR);
void delalias(const char *name)
{
    ident *id = idents->access(name);
    if(!id || id->type != ID_ALIAS) return;
    if(contextisolated[execcontext] && execcontext > id->context)
    {
        conoutf("cannot remove alias %s in this execution context", id->name);
        scripterr();
        return;
    }
    idents->remove(name);
}
COMMAND(delalias, ARG_1STR);

void alias(const char *name, const char *action)
{
    ident *b = idents->access(name);
    if(!b)
    {
        ident b(ID_ALIAS, newstring(name), newstring(action), persistidents, execcontext);
        idents->access(b.name, b);
    }
    else if(b->type==ID_ALIAS)
    {
        if(contextisolated[execcontext] && execcontext > b->context)
        {
            conoutf("cannot redefine alias %s in this execution context", b->name);
            scripterr();
            return;
        }
        if(b->isconst)
        {
            conoutf("alias %s is a constant and cannot be redefined", b->name);
            scripterr();
            return;
        }
        if(b->action!=b->executing) delete[] b->action;
        b->action = newstring(action);
        if(b->persist!=persistidents) b->persist = persistidents;
    }
    else
    {
        conoutf("cannot redefine builtin %s with an alias", name);
        scripterr();
    }
}

void constant(const char *name, const char *action)
{
    ident *b = idents->access(name);
    if(!b)
    {
        ident b(ID_ALIAS, newstring(name), newstring(action), persistidents, execcontext);
        b.isconst = true;
        idents->access(b.name, b);
        return;
    }
    if(b->isconst)
    {
        conoutf("%s is already defined as a constant", b->name);
        scripterr();
        return;
    }
    if(b->type==ID_ALIAS)
    {
        if(contextisolated[execcontext] && execcontext > b->context)
        {
            conoutf("cannot redefine alias %s as a constant in this execution context", b->name);
            scripterr();
            return;
        }
        b->isconst = true;
        if(action && action[0])
        {
            if(b->action!=b->executing) delete[] b->action;
            b->action = newstring(action);
            if(b->persist!=persistidents) b->persist = persistidents;
        }
    }
    else
    {
        conoutf("cannot redefine %s as a constant", name);
        scripterr();
    }
}

COMMAND(alias, ARG_2STR);
COMMANDN(const, constant, ARG_2STR);

COMMANDF(checkalias, ARG_1STR, (const char *name) { intret(getalias(name) ? 1 : 0); });
COMMANDF(isconst, ARG_1STR, (const char *name) { ident *id = idents->access(name); intret(id && id->isconst ? 1 : 0); });

// variable's and commands are registered through globals, see cube.h

int variable(const char *name, int minval, int cur, int maxval, int *storage, void (*fun)(), bool persist)
{
    if(!idents) idents = new hashtable<const char *, ident>;
    ident v(ID_VAR, name, minval, maxval, storage, fun, persist, IEXC_CORE);
    idents->access(name, v);
    return cur;
}

float fvariable(const char *name, float minval, float cur, float maxval, float *storage, void (*fun)(), bool persist)
{
    if(!idents) idents = new hashtable<const char *, ident>;
    ident v(ID_FVAR, name, minval, maxval, storage, fun, persist, IEXC_CORE);
    idents->access(name, v);
    return cur;
}

char *svariable(const char *name, const char *cur, char **storage, void (*fun)(), bool persist)
{
    if(!idents) idents = new hashtable<const char *, ident>;
    ident v(ID_SVAR, name, storage, fun, persist, IEXC_CORE);
    idents->access(name, v);
    return newstring(cur);
}

#define _GETVAR(id, vartype, name, retval) \
    ident *id = idents->access(name); \
    if(!id || id->type!=vartype) return retval;
#define GETVAR(id, name, retval) _GETVAR(id, ID_VAR, name, retval)
void setvar(const char *name, int i, bool dofunc)
{
    GETVAR(id, name, );
    *id->storage.i = clamp(i, id->minval, id->maxval);
    if(dofunc && id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
}
void setfvar(const char *name, float f, bool dofunc)
{
    _GETVAR(id, ID_FVAR, name, );
    *id->storage.f = clamp(f, id->minvalf, id->maxvalf);
    if(dofunc && id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
}
void setsvar(const char *name, const char *str, bool dofunc)
{
    _GETVAR(id, ID_SVAR, name, );
    *id->storage.s = exchangestr(*id->storage.s, str);
    if(dofunc && id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
}

void modifyvar(const char *name, const char *arg, char op)
{
    ident *id = idents->access(name);
    if(!id) return;
    if(!allowidentaccess(id))
    {
        conoutf("not allowed in this execution context: %s", id->name);
        scripterr();
        return;
    }
    int val = 0;
    switch(id->type)
    {
        case ID_VAR: val = *id->storage.i; break;
        case ID_FVAR: val = int(*id->storage.f); break;
        case ID_SVAR: val = ATOI(*id->storage.s); break;
        case ID_ALIAS: val = ATOI(id->action); break;
    }
    int argval = ATOI(arg);
    switch(op)
    {
        case '+': val += argval; break;
        case '-': val -= argval; break;
        case '*': val *= argval; break;
        case '/': val /= argval; break;
    }
    switch(id->type)
    {
        case ID_VAR: *id->storage.i = clamp(val, id->minval, id->maxval); break;
        case ID_FVAR: *id->storage.f = clamp((float)val, id->minvalf, id->maxvalf); break;
        case ID_SVAR: { string str; itoa(str, val); *id->storage.s = exchangestr(*id->storage.s, str); break; }
        case ID_ALIAS: { string str; itoa(str, val); alias(name, str); return; }
        default: return;
    }
    if(id->fun) ((void (__cdecl *)())id->fun)();
}

void modifyfvar(const char *name, const char *arg, char op)
{
    ident *id = idents->access(name);
    if(!id) return;
    if(!allowidentaccess(id))
    {
        conoutf("not allowed in this execution context: %s", id->name);
        scripterr();
        return;
    }
    float val = 0;
    switch(id->type)
    {
        case ID_VAR: val = *id->storage.i; break;
        case ID_FVAR: val = *id->storage.f; break;
        case ID_SVAR: val = atof(*id->storage.s); break;
        case ID_ALIAS: val = atof(id->action); break;
    }
    float argval = atof(arg);
    switch(op)
    {
        case '+': val += argval; break;
        case '-': val -= argval; break;
        case '*': val *= argval; break;
        case '/': val /= argval; break;
    }
    switch(id->type)
    {
        case ID_VAR: *id->storage.i = clamp((int)val, id->minval, id->maxval); break;
        case ID_FVAR: *id->storage.f = clamp(val, id->minvalf, id->maxvalf); break;
        case ID_SVAR: *id->storage.s = exchangestr(*id->storage.s, floatstr(val)); break;
        case ID_ALIAS: alias(name, floatstr(val)); return;
        default: return;
    }
    if(id->fun) ((void (__cdecl *)())id->fun)();
}

void addeq(char *name, char *arg) { modifyvar(name, arg, '+'); }
void subeq(char *name, char *arg) { modifyvar(name, arg, '-'); }
void muleq(char *name, char *arg) { modifyvar(name, arg, '*'); }
void addeqf(char *name, char *arg) { modifyfvar(name, arg, '+'); }
void subeqf(char *name, char *arg) { modifyfvar(name, arg, '-'); }
void muleqf(char *name, char *arg) { modifyfvar(name, arg, '*'); }

COMMANDN(+=, addeq, ARG_2STR);
COMMANDN(-=, subeq, ARG_2STR);
COMMANDN(*=, muleq, ARG_2STR);
COMMANDN(+=f, addeqf, ARG_2STR);
COMMANDN(-=f, subeqf, ARG_2STR);
COMMANDN(*=f, muleqf, ARG_2STR);

int getvar(const char *name)
{
    GETVAR(id, name, 0);
    return *id->storage.i;
}

bool identexists(const char *name) { return idents->access(name)!=NULL; }

const char *getalias(const char *name)
{
    ident *i = idents->access(name);
    return i && i->type==ID_ALIAS ? i->action : NULL;
}
void _getalias(char *name)
{
    string o;
    ident *id = idents->access(name);
    const char *action = getalias(name);
    if(id)
    {
        switch(id->type)
        {
            case ID_VAR:
                formatstring(o)("%d", *id->storage.i);
                result(o);
                break;
            case ID_FVAR:
                formatstring(o)("%.3f", *id->storage.f);
                result(o);
                break;
            case ID_SVAR:
                formatstring(o)("%s", *id->storage.s);
                result(o);
                break;
            case ID_ALIAS:
                result(action ? action : "");
                break;
            default: break;
        }
    }
}
COMMANDN(getalias, _getalias, ARG_1STR);

// (getalias) now handles client variables as well
/*void _getvar(char *name)
{
    string resstr;
    resstr[0] = '\0';
    ident *id = idents->access(name);
    if(id)
    {
        switch(id->type)
        {
            case ID_VAR:
                formatstring(resstr)("%d", *id->storage.i);
                break;
            case ID_FVAR:
                formatstring(resstr)("%.3f", *id->storage.f);
                break;
            case ID_SVAR:
                formatstring(resstr)("%s", *id->storage.s);
                break;
            default: break;
        }
    }
    result(resstr);
}
COMMANDN(getvar, _getvar, ARG_1STR);*/
COMMANDN(isIdent, identexists, ARG_1EST);

bool addcommand(const char *name, void (*fun)(), int narg)
{
    if(!idents) idents = new hashtable<const char *, ident>;
    ident c(ID_COMMAND, name, fun, narg, IEXC_CORE);
    idents->access(name, c);
    return false;
}

char *parseexp(const char *&p, int right)             // parse any nested set of () or []
{
    int left = *p++;
    const char *word = p;
    bool quot = false;
    for(int brak = 1; brak; )
    {
        int c = *p++;
        if(c==left && !quot) brak++;
        else if(c=='"') quot = !quot;
        else if(c==right && !quot) brak--;
        else if(!c)
        {
            p--;
            conoutf("missing \"%c\"", right);
            scripterr();
            return NULL;
        }
    }
    char *s = newstring(word, p-word-1);
    if(left=='(')
    {
        char *ret = executeret(s); // evaluate () exps directly, and substitute result
        delete[] s;
        s = ret ? ret : newstring("");
    }
    return s;
}

char *lookup(char *n)                           // find value of ident referenced with $ in exp
{
    ident *id = idents->access(n+1);
    if(id) switch(id->type)
    {
        case ID_VAR: { string t; itoa(t, *id->storage.i); return exchangestr(n, t); }
        case ID_FVAR: return exchangestr(n, floatstr(*id->storage.f));
        case ID_SVAR: return exchangestr(n, *id->storage.s);
        case ID_ALIAS: return exchangestr(n, id->action);
    }
    conoutf("unknown alias lookup: %s", n+1);
    scripterr();
    return n;
}

char *parseword(const char *&p, int arg, int &infix)                       // parse single argument, including expressions
{
    p += strspn(p, " \t\r");
    if(p[0]=='/' && p[1]=='/') p += strcspn(p, "\n\0");
    if(*p=='\"')
    {
        p++;
        const char *word = p;
        p += strcspn(p, "\"\r\n\0");
        char *s = newstring(word, p-word);
        if(*p=='\"') p++;
        return s;
    }
    if(*p=='(') return parseexp(p, ')');
    if(*p=='[') return parseexp(p, ']');
    const char *word = p;
    p += strcspn(p, "; \t\r\n\0");
    if(p-word==0) return NULL;
    if(arg==1 && p-word==1) switch(*word)
    {
        case '=': infix = *word; break;
    }
    char *s = newstring(word, p-word);
    if(*s=='$') return lookup(s);
    return s;
}

char *conc(char **w, int n, bool space)
{
    int len = space ? max(n-1, 0) : 0;
    loopj(n) len += (int)strlen(w[j]);
    char *r = newstring("", len);
    loopi(n)
    {
        strcat(r, w[i]);  // make string-list out of all arguments
        if(i==n-1) break;
        bool col = w[i][0] == '\f' && w[i][2] == '\0';
        if(space && !col) strcat(r, " ");
    }
    return r;
}

VARN(numargs, _numargs, 25, 0, 0);

char *commandret = NULL;

void intret(int v)
{
    string t;
    itoa(t, v);
    commandret = newstring(t);
}

const char *floatstr(float v)
{
    static string l;
    ftoa(l, 0.5);
    static int n = 0;
    static string t[3];
    n = (n + 1)%3;
    ftoa(t[n], v);
    return t[n];
}


void floatret(float v)
{
    commandret = newstring(floatstr(v));
}

void result(const char *s) { commandret = newstring(s); }

#if 0
// seer : script evaluation excessive recursion
static int seer_count = 0; // count calls to executeret, check time every n1 (100) calls
static int seer_index = -1; // position in timestamp vector
vector<long long> seer_t1; // timestamp of last n2 (10) level-1 calls
vector<long long> seer_t2; // timestamp of last n3 (10) level-2 calls
#endif
char *executeret(const char *p)                            // all evaluation happens here, recursively
{
    if(!p || !p[0]) return NULL;
    bool noproblem = true;
#if 0
    if(execcontext>IEXC_CFG) // only PROMPT and MAP-CFG are checked for this, fooling with core/cfg at your own risk!
    {
        seer_count++;
        if(seer_count>=100)
        {
            seer_index = (seer_index+1)%10;
            long long cts = (long long) time(NULL);
            if(seer_t1.length()>=10) seer_t1[seer_index] = cts;
            seer_t1.add(cts);
            int lc = (seer_index+11)%10;
            if(lc<=seer_t1.length())
            {
                int dt = seer_t1[seer_index] - seer_t1[lc];
                if(abs(dt)<2)
                {
                    conoutf("SCRIPT EXECUTION warning [%d:%s]", &p, p);
                    seer_t2.add(seer_t1[seer_index]);
                    if(seer_t2.length() >= 10)
                    {
                        if(seer_t2[0] == seer_t2.last())
                        {
                            conoutf("SCRIPT EXECUTION in danger of crashing the client - dropping script [%s].", p);
                            noproblem = false;
                            seer_t2.shrink(0);
                            seer_t1.shrink(0);
                            seer_index = 0;
                        }
                    }
                }
            }
            seer_count = 0;
        }
    }
#endif
    const int MAXWORDS = 25;                    // limit, remove
    char *w[MAXWORDS];
    char *retval = NULL;
    #define setretval(v) { char *rv = v; if(rv) retval = rv; }
    if(noproblem) // if the "seer"-algorithm doesn't object
    {
        for(bool cont = true; cont;)                // for each ; seperated statement
        {
            if(loop_level && loop_skip) break;
            int numargs = MAXWORDS, infix = 0;
            loopi(MAXWORDS)                         // collect all argument values
            {
                w[i] = (char *)"";
                if(i>numargs) continue;
                char *s = parseword(p, i, infix);   // parse and evaluate exps
                if(s) w[i] = s;
                else numargs = i;
            }

            p += strcspn(p, ";\n\0");
            cont = *p++!=0;                         // more statements if this isn't the end of the string
            const char *c = w[0];
            if(!*c) continue;                       // empty statement

            DELETEA(retval);

            if(infix)
            {
                switch(infix)
                {
                    case '=':
                        DELETEA(w[1]);
                        swap(w[0], w[1]);
                        c = "alias";
                        break;
                }
            }

            ident *id = idents->access(c);
            if(!id)
            {
                if(!isdigit(*c) && ((*c!='+' && *c!='-' && *c!='.') || !isdigit(c[1])))
                {
                    conoutf("unknown command: %s", c);
                    scripterr();
                }
                setretval(newstring(c));
            }
            else
            {
                if(!allowidentaccess(id))
                {
                    conoutf("not allowed in this execution context: %s", id->name);
                    scripterr();
                    continue;
                }

                switch(id->type)
                {
                    case ID_COMMAND:                    // game defined commands
                        switch(id->narg)                // use very ad-hoc function signature, and just call it
                        {
                            case ARG_1INT: ((void (__cdecl *)(int))id->fun)(ATOI(w[1])); break;
                            case ARG_2INT: ((void (__cdecl *)(int, int))id->fun)(ATOI(w[1]), ATOI(w[2])); break;
                            case ARG_3INT: ((void (__cdecl *)(int, int, int))id->fun)(ATOI(w[1]), ATOI(w[2]), ATOI(w[3])); break;
                            case ARG_4INT: ((void (__cdecl *)(int, int, int, int))id->fun)(ATOI(w[1]), ATOI(w[2]), ATOI(w[3]), ATOI(w[4])); break;
                            case ARG_NONE: ((void (__cdecl *)())id->fun)(); break;
                            case ARG_1STR: ((void (__cdecl *)(char *))id->fun)(w[1]); break;
                            case ARG_2STR: ((void (__cdecl *)(char *, char *))id->fun)(w[1], w[2]); break;
                            case ARG_3STR: ((void (__cdecl *)(char *, char *, char*))id->fun)(w[1], w[2], w[3]); break;
                            case ARG_4STR: ((void (__cdecl *)(char *, char *, char*, char*))id->fun)(w[1], w[2], w[3], w[4]); break;
                            case ARG_5STR: ((void (__cdecl *)(char *, char *, char*, char*, char*))id->fun)(w[1], w[2], w[3], w[4], w[5]); break;
                            case ARG_6STR: ((void (__cdecl *)(char *, char *, char*, char*, char*, char*))id->fun)(w[1], w[2], w[3], w[4], w[5], w[6]); break;
                            case ARG_7STR: ((void (__cdecl *)(char *, char *, char*, char*, char*, char*, char*))id->fun)(w[1], w[2], w[3], w[4], w[5], w[6], w[7]); break;
                            case ARG_8STR: ((void (__cdecl *)(char *, char *, char*, char*, char*, char*, char*, char*))id->fun)(w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]); break;
#ifndef STANDALONE
                            case ARG_DOWN: ((void (__cdecl *)(bool))id->fun)(addreleaseaction(id->name)!=NULL); break;
#endif
                            case ARG_1EXP: intret(((int (__cdecl *)(int))id->fun)(ATOI(w[1]))); break;
                            case ARG_2EXP: intret(((int (__cdecl *)(int, int))id->fun)(ATOI(w[1]), ATOI(w[2]))); break;
                            case ARG_1EXPF: floatret(((float (__cdecl *)(float))id->fun)(atof(w[1]))); break;
                            case ARG_2EXPF: floatret(((float (__cdecl *)(float, float))id->fun)(atof(w[1]), atof(w[2]))); break;
                            case ARG_1EST: intret(((int (__cdecl *)(char *))id->fun)(w[1])); break;
                            case ARG_2EST: intret(((int (__cdecl *)(char *, char *))id->fun)(w[1], w[2])); break;
                            case ARG_IVAL: intret(((int (__cdecl *)())id->fun)()); break;
                            case ARG_FVAL: floatret(((float (__cdecl *)())id->fun)()); break;
                            case ARG_SVAL: result(((const char * (__cdecl *)())id->fun)()); break;
                            case ARG_VARI: ((void (__cdecl *)(char **, int))id->fun)(&w[1], numargs-1); break;
                            case ARG_CONC:
                            case ARG_CONCW:
                            {
                                char *r = conc(w+1, numargs-1, id->narg==ARG_CONC);
                                ((void (__cdecl *)(char *))id->fun)(r);
                                delete[] r;
                                break;
                            }
                        }
                        setretval(commandret);
                        commandret = NULL;
                        break;

                    case ID_VAR:                        // game defined variables
                        if(!w[1][0]) conoutf("%s = %d", c, *id->storage.i);      // var with no value just prints its current value
                        else if(id->minval>id->maxval) conoutf("variable %s is read-only", id->name);
                        else
                        {
                            int i1 = ATOI(w[1]);
                            if(i1<id->minval || i1>id->maxval)
                            {
                                i1 = i1<id->minval ? id->minval : id->maxval;       // clamp to valid range
                                conoutf("valid range for %s is %d..%d", id->name, id->minval, id->maxval);
                            }
                            *id->storage.i = i1;
                            if(id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
                        }
                        break;

                    case ID_FVAR:                        // game defined variables
                        if(!w[1][0]) conoutf("%s = %s", c, floatstr(*id->storage.f));      // var with no value just prints its current value
                        else if(id->minvalf>id->maxvalf) conoutf("variable %s is read-only", id->name);
                        else
                        {
                            float f1 = atof(w[1]);
                            if(f1<id->minvalf || f1>id->maxvalf)
                            {
                                f1 = f1<id->minvalf ? id->minvalf : id->maxvalf;       // clamp to valid range
                                conoutf("valid range for %s is %s..%s", id->name, floatstr(id->minvalf), floatstr(id->maxvalf));
                                //scripterr(); // Why throw this error here when it's not done for ID_VAR above? Only difference is datatype, both are "valid range errors". // Bukz 2011june04
                            }
                            *id->storage.f = f1;
                            if(id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
                        }
                        break;

                    case ID_SVAR:                        // game defined variables
                        if(!w[1][0]) conoutf(strchr(*id->storage.s, '"') ? "%s = [%s]" : "%s = \"%s\"", c, *id->storage.s); // var with no value just prints its current value
                        else
                        {
                            *id->storage.s = exchangestr(*id->storage.s, newstring(w[1]));
                            if(id->fun) ((void (__cdecl *)())id->fun)();            // call trigger function if available
                        }
                        break;

                    case ID_ALIAS:                              // alias, also used as functions and (global) variables
                        delete[] w[0];
                        static vector<ident *> argids;
                        for(int i = 1; i<numargs; i++)
                        {
                            if(i > argids.length())
                            {
                                defformatstring(argname)("arg%d", i);
                                argids.add(newident(argname, IEXC_CORE));
                            }
                            pushident(*argids[i-1], w[i]); // set any arguments as (global) arg values so functions can access them
                        }
                        _numargs = numargs-1;
                        char *wasexecuting = id->executing;
                        id->executing = id->action;
                        setretval(executeret(id->action));
                        if(id->executing!=id->action && id->executing!=wasexecuting) delete[] id->executing;
                        id->executing = wasexecuting;
                        for(int i = 1; i<numargs; i++) popident(*argids[i-1]);
                        continue;
                }
            }
            loopj(numargs) if(w[j]) delete[] w[j];
        }
    }
    return retval;
}

int execute(const char *p)
{
    char *ret = executeret(p);
    int i = 0;
    if(ret) { i = ATOI(ret); delete[] ret; }
    return i;
}

#ifndef STANDALONE
// tab-completion of all idents

static int completesize = -1, completeidx = 0;
static playerent *completeplayer = NULL;

void resetcomplete()
{
    completesize = -1;
    completeplayer = NULL;
}

bool nickcomplete(char *s)
{
    if(!players.length()) return false;

    char *cp = s;
    for(int i = (int)strlen(s) - 1; i > 0; i--)
        if(s[i] == ' ') { cp = s + i + 1; break; }
    if(completesize < 0) { completesize = (int)strlen(cp); completeidx = 0; }

    int idx = 0;
    if(completeplayer!=NULL)
    {
        idx = players.find(completeplayer)+1;
        if(!players.inrange(idx)) idx = 0;
    }

    for(int i=idx; i<idx+players.length(); i++)
    {
        playerent *p = players[i % players.length()];
        if(p && !strncasecmp(p->name, cp, completesize))
        {
            *cp = '\0';
            concatstring(s, p->name);
            completeplayer = p;
            return true;
        }
    }
    return false;
}

enum { COMPLETE_FILE = 0, COMPLETE_LIST, COMPLETE_NICK };

struct completekey
{
    int type;
    const char *dir, *ext;

    completekey() {}
    completekey(int type, const char *dir, const char *ext) : type(type), dir(dir), ext(ext) {}
};

struct completeval
{
    int type;
    char *dir, *ext;
    vector<char *> dirlist;
    vector<char *> list;

    completeval(int type, const char *dir, const char *ext) : type(type), dir(dir && dir[0] ? newstring(dir) : NULL), ext(ext && ext[0] ? newstring(ext) : NULL) {}
    ~completeval() { DELETEA(dir); DELETEA(ext); dirlist.deletearrays(); list.deletearrays(); }
};

static inline bool htcmp(const completekey &x, const completekey &y)
{
    return x.type==y.type && (x.dir == y.dir || (x.dir && y.dir && !strcmp(x.dir, y.dir))) && (x.ext == y.ext || (x.ext && y.ext && !strcmp(x.ext, y.ext)));
}

static inline uint hthash(const completekey &k)
{
    return k.dir ? hthash(k.dir) + k.type : k.type;
}

static hashtable<completekey, completeval *> completedata;
static hashtable<char *, completeval *> completions;

void addcomplete(char *command, int type, char *dir, char *ext)
{
    if(type==COMPLETE_FILE)
    {
        int dirlen = (int)strlen(dir);
        while(dirlen > 0 && (dir[dirlen-1] == '/' || dir[dirlen-1] == '\\'))
            dir[--dirlen] = '\0';
        if(ext)
        {
            if(strchr(ext, '*')) ext[0] = '\0';
            if(!ext[0]) ext = NULL;
        }
    }
    completekey key(type, dir, ext);
    completeval **val = completedata.access(key);
    if(!val)
    {
        completeval *f = new completeval(type, dir, ext);
        if(type==COMPLETE_LIST) explodelist(dir, f->list);
        if(type==COMPLETE_FILE)
        {
            explodelist(dir, f->dirlist);
            loopv(f->dirlist)
            {
                char *dir = f->dirlist[i];
                int dirlen = (int)strlen(dir);
                while(dirlen > 0 && (dir[dirlen-1] == '/' || dir[dirlen-1] == '\\'))
                    dir[--dirlen] = '\0';
            }
        }
        val = &completedata[completekey(type, f->dir, f->ext)];
        *val = f;
    }
    completeval **hascomplete = completions.access(command);
    if(hascomplete) *hascomplete = *val;
    else completions[newstring(command)] = *val;
}

void addfilecomplete(char *command, char *dir, char *ext)
{
    addcomplete(command, COMPLETE_FILE, dir, ext);
}

void addlistcomplete(char *command, char *list)
{
    addcomplete(command, COMPLETE_LIST, list, NULL);
}

void addnickcomplete(char *command)
{
    addcomplete(command, COMPLETE_NICK, NULL, NULL);
}

COMMANDN(complete, addfilecomplete, ARG_3STR);
COMMANDN(listcomplete, addlistcomplete, ARG_2STR);
COMMANDN(nickcomplete, addnickcomplete, ARG_1STR);

void commandcomplete(char *s)
{
    if(*s!='/')
    {
        string t;
        copystring(t, s);
        copystring(s, "/");
        concatstring(s, t);
    }
    if(!s[1]) return;
    char *cp = s;
    for(int i = (int)strlen(s) - 1; i > 0; i--)
        if(s[i] == ';' || s[i] == ' ') { cp = s + i; break; }
    bool init = false;
    if(completesize < 0)
    {
        completesize = (int)strlen(cp)-1;
        completeidx = 0;
        if(*cp == ' ') init = true;
    }

    completeval *cdata = NULL;
    char *end = strchr(s, ' ');
    if(end && end <= cp)
    {
        string command;
        copystring(command, s+1, min(size_t(end-s), sizeof(command)));
        completeval **hascomplete = completions.access(command);
         if(hascomplete) cdata = *hascomplete;
    }
    if(init && cdata && cdata->type==COMPLETE_FILE)
    {
       cdata->list.deletearrays();
       loopv(cdata->dirlist) listfiles(cdata->dirlist[i], cdata->ext, cdata->list);
    }

    if(*cp == '/' || *cp == ';')
    { // commandname completion
        int idx = 0;
        enumerate(*idents, ident, id,
            if(!strncasecmp(id.name, cp+1, completesize) && idx++==completeidx)
            {
                cp[1] = '\0';
                concatstring(s, id.name);
            }
        );
        completeidx++;
        if(completeidx>=idx) completeidx = 0;
    }
    else if(!cdata) return;
    else if(cdata->type==COMPLETE_NICK) nickcomplete(s);
    else
    { // argument completion
        loopv(cdata->list)
        {
            int j = (i + completeidx) % cdata->list.length();
            if(!strncasecmp(cdata->list[j], cp + 1, completesize))
            {
                cp[1] = '\0';
                concatstring(s, cdata->list[j]);
                completeidx = j;
                break;
            }
        }
        completeidx++;
        if(completeidx >= cdata->list.length()) completeidx = 0;
    }
}

void complete(char *s)
{
    if(*s!='/')
    {
        if(nickcomplete(s)) return;
    }
    commandcomplete(s);
}
#endif

const char *curcontext = NULL, *curinfo = NULL;

void scripterr()
{
    if(curcontext) conoutf("(%s: %s)", curcontext, curinfo);
    else conoutf("(from console or builtin)");
}

void setcontext(const char *context, const char *info)
{
    curcontext = context;
    curinfo = info;
}

void resetcontext()
{
    curcontext = curinfo = NULL;
}

bool execfile(const char *cfgfile)
{
    string s;
    copystring(s, cfgfile);
    setcontext("file", cfgfile);
    char *buf = loadfile(path(s), NULL);
    if(!buf)
    {
        resetcontext();
        return false;
    }
    execute(buf);
    delete[] buf;
    resetcontext();
    return true;
}

void exec(const char *cfgfile)
{
    if(!execfile(cfgfile)) conoutf("could not read \"%s\"", cfgfile);
}

void execdir(const char *dir)
{
        if(dir[0])
        {
            vector<char *> files;
            listfiles(dir, "cfg", files);
            loopv(files)
            {
                defformatstring(d)("%s/%s.cfg",dir,files[i]);
                exec(d);
            }
        }
}
COMMAND(execdir,ARG_1STR);
// below the commands that implement a small imperative language. thanks to the semantics of
// () and [] expressions, any control construct can be defined trivially.

void ifthen(char *cond, char *thenp, char *elsep) { commandret = executeret(cond[0]!='0' ? thenp : elsep); }
void loopa(char *var, char *times, char *body)
{
    int t = ATOI(times);
    if(t<=0) return;
    ident *id = newident(var, execcontext);
    if(id->type!=ID_ALIAS) return;
    char *buf = newstring("0", 16);
    pushident(*id, buf);
    loop_level++;
    execute(body);
    if(loop_skip) loop_skip = false;
    if(loop_break) loop_break = false;
    else
    {
        loopi(t-1)
        {
            if(buf != id->action)
            {
                if(id->action != id->executing) delete[] id->action;
                id->action = buf = newstring(16);
            }
            itoa(id->action, i+1);
            execute(body);
            if(loop_skip) loop_skip = false;
            if(loop_break)
            {
                loop_break = false;
                break;
            }
        }
    }
    popident(*id);
    loop_level--;
}
void whilea(char *cond, char *body)
{
    loop_level++;
    while(execute(cond))
    {
        execute(body);
        if(loop_skip) loop_skip = false;
        if(loop_break)
        {
            loop_break = false;
            break;
        }
    }
    loop_level--;
}

void breaka() { if(loop_level) loop_skip = loop_break = true; }
void continuea() { if(loop_level) loop_skip = true; }

void concat(char *s) { result(s); }
void concatword(char *s) { result(s); }

void format(char **args, int numargs)
{
    if(numargs < 1)
    {
        result("");
        return;
    }

    vector<char> s;
    char *f = args[0];
    while(*f)
    {
        int c = *f++;
        if(c == '%')
        {
            int i = *f++;
            if(i >= '1' && i <= '9')
            {
                i -= '0';
                const char *sub = i < numargs ? args[i] : "";
                while(*sub) s.add(*sub++);
            }
            else s.add(i);
        }
        else s.add(c);
    }
    s.add('\0');
    result(s.getbuf());
}

#define whitespaceskip s += strspn(s, "\n\t \r")
#define elementskip *s=='"' ? (++s, s += strcspn(s, "\"\n\0"), s += *s=='"') : s += strcspn(s, "\n\t \0")

void explodelist(const char *s, vector<char *> &elems)
{
    whitespaceskip;
    while(*s)
    {
        const char *elem = s;
        elementskip;
        elems.add(*elem=='"' ? newstring(elem+1, s-elem-(s[-1]=='"' ? 2 : 1)) : newstring(elem, s-elem));
        whitespaceskip;
    }
}

void looplist(char *list, char *var, char *body)
{
    ident *id = newident(var, execcontext);
    if(id->type!=ID_ALIAS) return;
    char *buf = newstring(MAXSTRLEN);

    vector<char *> elems;
    explodelist(list, elems);

    loop_level++;
    loopv(elems)
    {
        const char *elem = elems[i];
        if(buf != id->action)
        {
            if(id->action != id->executing) delete[] id->action;
            id->action = buf = newstring(MAXSTRLEN);
        }
        copystring(id->action, elem);
        execute(body);
        if(loop_skip) loop_skip = false;
        if(loop_break)
        {
            loop_break = false;
            break;
        }
    }
    popident(*id);
    loop_level--;
}

char *indexlist(const char *s, int pos)
{
    whitespaceskip;
    loopi(pos)
    {
        elementskip;
        whitespaceskip;
        if(!*s) break;
    }
    const char *e = s;
    elementskip;
    if(*e=='"')
    {
        e++;
        if(s[-1]=='"') --s;
    }
    return newstring(e, s-e);
}

int listlen(char *s)
{
    int n = 0;
    whitespaceskip;
    for(; *s; n++) elementskip, whitespaceskip;
    return n;
}

void at(char *s, char *pos)
{
    commandret = indexlist(s, ATOI(pos));
}

int find(const char *s, const char *key)
{
    whitespaceskip;
    int len = strlen(key);
    for(int i = 0; *s; i++)
    {
        const char *a = s, *e = s;
        elementskip;
        if(*e=='"')
        {
            e++;
            if(s[-1]=='"') --s;
        }
        if(s-e==len && !strncmp(e, key, s-e)) return i;
        else s = a;
        elementskip, whitespaceskip;
    }
    return -1;
}

void findlist(char *s, char *key)
{
    intret(find(s, key));
}
void colora(char *s)
{
    if(s[0] && s[1]=='\0') {
    defformatstring(x)("\f%c",s[0]);
    commandret = newstring(x);
    }
}

// Easily inject a string into various CubeScript punctuations
void addpunct(char *s, char *type = "0")
{
    int t = atoi(type);
    if(t > -1 && t < 6)
    {
        switch(t)
        {
            case 1:  defformatstring(o1)("[%s]", s);   result(o1); break;
            case 2:  defformatstring(o2)("(%s)", s);   result(o2); break;
            case 3:  defformatstring(o3)("$%s", s);    result(o3); break;
            case 4:  result("\""); break;
            case 5:  result("%");  break;
            default: defformatstring(o4)("\"%s\"", s); result(o4); break;
        }
    }
}

void toLower(char *s) { result(strcaps(s, false)); }
void toUpper(char *s) { result(strcaps(s, true)); }

void testchar(char *s, char *type)
{
    int t = atoi(type);
    switch(t) {
        case 1:
            if(isalpha(s[0]) != 0) { intret(1); return; }
            break;
        case 2:
            if(isalnum(s[0]) != 0) { intret(1); return; }
            break;
        case 3:
            if(islower(s[0]) != 0) { intret(1); return; }
            break;
        case 4:
            if(isupper(s[0]) != 0) { intret(1); return; }
            break;
        case 5:
            if(isprint(s[0]) != 0) { intret(1); return; }
            break;
        case 6:
            if(ispunct(s[0]) != 0) { intret(1); return; }
            break;
        case 7:
            if(isspace(s[0]) != 0) { intret(1); return; }
            break;
        default:
            if(isdigit(s[0]) != 0) { intret(1); return; }
            break;
    }
    intret(0);
}

// test a cubescript list for various conditions, default being - ensure that every element of the list
// is a number (supports ints and floats)
void testlist(char *list, char *type = "0")
{
    if(listlen(list)) {
        int t = atoi(type);

        for(int ctr = 0; ctr < listlen(list); ctr++) { // loop through every element of the list
            int periodCtr = 0; // counter for how many times a period (.) shows up in each element
            char *curelement = indexlist(list, ctr); // the current element to examine

            for(unsigned int len = 0; len < strlen(curelement); len++) { // loop through the length (in chars) of the current element
                char curchar = curelement[len]; // the current char of the current element to examine

                switch(t) {
                    case 1: // test for a list of alpha characters (a-z || A-Z are valid)
                        if(!isalpha(curchar)) { intret(0); delete[] curelement; return; }
                        break;
                    case 2: // test for a list of alhpa characters (a-z || A-Z || whitespace are valid)
                        if(!isalpha(curchar) && !isspace(curchar)) { intret(0); delete[] curelement; return; }
                        break;
                    //case 3: break;
                    default: // test for a list of valid numbers
                        bool notvalid = (isalpha(curchar) || isspace(curchar) || (ispunct(curchar) && curchar != '.')); // determines if the char is valid or not

                        if(curchar == '.') periodCtr++; // increment the periods counter
                        if(notvalid || periodCtr > 1) { intret(0); delete[] curelement; return; } // if the char is not valid or we have found more than 1 period in this element, it is an invalid list
                        break;
                }
            }
            delete[] curelement;
        }

        intret(1);
    }
}

void strreplace (const char *source, const char *search, const char *replace)
{
    vector<char> buf;

    int searchlen = strlen(search);
    if(!searchlen) { result(source); return; }
    for(;;)
    {
        const char *found = strstr(source, search);
        if(found)
        {
            while(source < found) buf.add(*source++);
            for(const char *n = replace; *n; n++) buf.add(*n);
            source = found + searchlen;
        }
        else
        {
            while(*source) buf.add(*source++);
            buf.add('\0');
            result(buf.getbuf());
            return;
        }
    }
}

COMMANDN(c, colora, ARG_1STR);
COMMANDN(loop, loopa, ARG_3STR);
COMMAND(looplist, ARG_3STR);
COMMANDN(while, whilea, ARG_2STR);
COMMANDN(break, breaka, ARG_NONE);
COMMANDN(continue, continuea, ARG_NONE);
COMMANDN(if, ifthen, ARG_3STR);
COMMAND(exec, ARG_1STR);
COMMAND(concat, ARG_CONC);
COMMAND(concatword, ARG_CONCW);
COMMAND(format, ARG_VARI);
COMMAND(result, ARG_1STR);
COMMAND(execute, ARG_1STR);
COMMAND(at, ARG_2STR);
COMMAND(listlen, ARG_1EST);
COMMAND(findlist, ARG_2STR);
COMMAND(addpunct, ARG_2STR);
COMMANDN(tolower, toLower, ARG_1STR);
COMMANDN(toupper, toUpper, ARG_1STR);
COMMAND(testchar, ARG_2STR);
COMMAND(testlist, ARG_2STR);
COMMAND(strreplace, ARG_3STR);

int add(int a, int b)   { return a+b; }            COMMANDN(+, add, ARG_2EXP);
int mul(int a, int b)   { return a*b; }            COMMANDN(*, mul, ARG_2EXP);
int sub(int a, int b)   { return a-b; }            COMMANDN(-, sub, ARG_2EXP);
int div_(int a, int b)  { return b ? a/b : 0; }    COMMANDN(div, div_, ARG_2EXP);
int mod_(int a, int b)   { return b ? a%b : 0; }    COMMANDN(mod, mod_, ARG_2EXP);
float addf(float a, float b)   { return a+b; }            COMMANDN(+f, addf, ARG_2EXPF);
float mulf(float a, float b)   { return a*b; }            COMMANDN(*f, mulf, ARG_2EXPF);
float subf(float a, float b)   { return a-b; }            COMMANDN(-f, subf, ARG_2EXPF);
float divf_(float a, float b)  { return b ? a/b : 0; }    COMMANDN(divf, divf_, ARG_2EXPF);
float modf_(float a, float b)   { return b ? fmod(a, b) : 0; }    COMMANDN(modf, modf_, ARG_2EXPF);
float powf_(float a, float b)   { return powf(a, b); }    COMMANDN(powf, powf_, ARG_2EXPF);
int not_(int a) { return (int)(!a); }              COMMANDN(!, not_, ARG_1EXP);
int equal(int a, int b) { return (int)(a==b); }    COMMANDN(=, equal, ARG_2EXP);
int notequal(int a, int b) { return (int)(a!=b); } COMMANDN(!=, notequal, ARG_2EXP);
int lt(int a, int b)    { return (int)(a<b); }     COMMANDN(<, lt, ARG_2EXP);
int gt(int a, int b)    { return (int)(a>b); }     COMMANDN(>, gt, ARG_2EXP);
int lte(int a, int b)    { return (int)(a<=b); }   COMMANDN(<=, lte, ARG_2EXP);
int gte(int a, int b)    { return (int)(a>=b); }   COMMANDN(>=, gte, ARG_2EXP);

COMMANDF(round, ARG_1STR, (char *a) { intret((int)round(atof(a))); });
COMMANDF(ceil, ARG_1STR, (char *a) { intret((int)ceil(atof(a))); });
COMMANDF(floor, ARG_1STR, (char *a) { intret(atoi(a)); });

#define COMPAREF(opname, func, op) \
    void func(char *a, char *b) { intret((int)(atof(a) op atof(b))); } \
    COMMANDN(opname, func, ARG_2STR)
COMPAREF(=f, equalf, ==);
COMPAREF(!=f, notequalf, !=);
COMPAREF(<f, ltf, <);
COMPAREF(>f, gtf, >);
COMPAREF(<=f, ltef, <=);
COMPAREF(>=f, gtef, >=);

void anda (char *a, char *b) { intret(execute(a)!=0 && execute(b)!=0); }
void ora  (char *a, char *b) { intret(execute(a)!=0 || execute(b)!=0); }

COMMANDN(&&, anda, ARG_2STR);
COMMANDN(||, ora, ARG_2STR);

int strcmpa(char *a, char *b) { return strcmp(a,b)==0; }  COMMANDN(strcmp, strcmpa, ARG_2EST);

int rndn(int a)    { return a>0 ? rnd(a) : 0; }  COMMANDN(rnd, rndn, ARG_1EXP);

#ifndef STANDALONE
void writecfg()
{
    stream *f = openfile(path("config/saved.cfg", true), "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// delete this file to have defaults.cfg overwrite these settings\n// modify settings in game, or put settings in autoexec.cfg to override anything\n\n");
    f->printf("name \"%s\"\n", player1->name);
    extern const char *crosshairnames[CROSSHAIR_NUM];
    extern Texture *crosshairs[CROSSHAIR_NUM];
    loopi(CROSSHAIR_NUM) if(crosshairs[i] && crosshairs[i]!=notexture)
    {
        const char *fname = crosshairs[i]->name+strlen("packages/crosshairs/");
        if(i==CROSSHAIR_DEFAULT) f->printf("loadcrosshair %s\n", fname);
        else f->printf("loadcrosshair %s %s\n", fname, crosshairnames[i]);
    }
    extern int lowfps, highfps;
    f->printf("fpsrange %d %d\n", lowfps, highfps);
    extern string myfont;
    f->printf("setfont %s\n", myfont);
    f->printf("\n");
    audiomgr.writesoundconfig(f);
    f->printf("\n");
    enumerate(*idents, ident, id,
        if(!id.persist) continue;
        switch(id.type)
        {
            case ID_VAR: f->printf("%s %d\n", id.name, *id.storage.i); break;
            case ID_FVAR: f->printf("%s %s\n", id.name, floatstr(*id.storage.f)); break;
            case ID_SVAR: f->printf("%s [%s]\n", id.name, *id.storage.s); break;
        }
    );
    f->printf("\n");
    writebinds(f);
    f->printf("\n");
    enumerate(*idents, ident, id,
        if(id.type==ID_ALIAS && id.persist && id.action[0])
        {
            f->printf("%s = [%s]\n", id.name, id.action);
        }
    );
    f->printf("\n");
    delete f;
}

COMMAND(writecfg, ARG_NONE);

void deletecfg()
{
    string configs[] = { "config/saved.cfg", "config/init.cfg" };
    loopj(2) // delete files in homedir and basedir if possible
    {
        loopi(sizeof(configs)/sizeof(configs[0]))
        {
            const char *file = findfile(path(configs[i], true), "r");
            if(!file) continue;
            delfile(file);
        }
    }
}
#endif

void identnames(vector<const char *> &names, bool builtinonly)
{
    enumeratekt(*idents, const char *, name, ident, id,
    {
        if(!builtinonly || id.type != ID_ALIAS) names.add(name);
    });
}

void pushscontext(int newcontext)
{
    contextstack.add(execcontext);
    execcontext = newcontext;
}

int popscontext()
{
    ASSERT(contextstack.length() > 0);
    int old = execcontext;
    execcontext = contextstack.pop();

    if(execcontext < old && old >= IEXC_MAPCFG) // clean up aliases created in the old (map cfg) context
    {
        int limitcontext = max(execcontext + 1, (int) IEXC_MAPCFG);  // don't clean up below IEXC_MAPCFG
        enumeratekt(*idents, const char *, name, ident, id,
        {
            if(id.type == ID_ALIAS && id.context >= limitcontext)
            {
                while(id.stack && id.stack->context >= limitcontext)
                    popident(id);
                if(id.context >= limitcontext)
                {
                    if(id.action != id.executing) delete[] id.action;
                    idents->remove(name);
                }
            }
        });
    }
    return execcontext;
}

void scriptcontext(char *context, char *idname)
{
    if(contextsealed) return;
    ident *id = idents->access(idname);
    if(!id) return;
    int c = atoi(context);
    if(c >= 0 && c < IEXC_NUM) id->context = c;
}

void isolatecontext(int context)
{
    if(context >= 0 && context < IEXC_NUM && !contextsealed) contextisolated[context] = true;
}

void sealcontexts() { contextsealed = true; }

bool allowidentaccess(ident *id) // check if ident is allowed in current context
{
    ASSERT(execcontext >= 0 && execcontext < IEXC_NUM);
    if(!id) return false;
    if(!contextisolated[execcontext]) return true; // only check if context is isolated
    return execcontext <= id->context;
}

COMMAND(scriptcontext, ARG_2STR);
COMMAND(isolatecontext, ARG_1INT);
COMMAND(sealcontexts, ARG_NONE);

#ifndef STANDALONE
void _watchingdemo()
{
    extern bool watchingdemo;
    intret(watchingdemo);
}
COMMANDN(watchingdemo, _watchingdemo, ARG_NONE);

void systime()
{
    result(numtime());
}

void timestamp_()
{
    result(timestring(true, "%Y %m %d %H %M %S"));
}

void datestring()
{
    result(timestring(true, "%c"));
}

void timestring_()
{
    const char *res = timestring(true, "%H:%M:%S");
    result(res[0] == '0' ? res + 1 : res);
}

extern int millis_() { extern int totalmillis; return totalmillis; }
void strlen_(char *s) { string r; formatstring(r)("%d", strlen(s)); result(r); }

void substr_(char *fs, char *pa, char *len)
{
    int ia = atoi(pa);
    int ilen = atoi(len);
    int fslen = (int)strlen(fs);
    if(ia<0) ia += fslen;
    if(ia>fslen || ia < 0 || ilen < 0) return;

    if(!ilen) ilen = fslen-ia;
    (fs+ia)[ilen] = '\0';
    result(fs+ia);
}

void strpos_(char *haystack, char *needle, char *occurence)
{
    int position = -1;
    char *ptr = haystack;

    if(haystack && needle)
    for(int iocc = atoi(occurence); iocc >= 0; iocc--)
    {
        ptr = strstr(ptr, needle);
        if (ptr)
        {
            position = ptr-haystack;
            ptr += strlen(needle);
        }
        else
        {
            position = -1;
            break;
        }
    }
    intret(position);
}

void l0(int p, int v) { string f; string r; formatstring(f)("%%0%dd", p); formatstring(r)(f, v); result(r); }

void getmode_(int acr) { result(modestr(gamemode, acr ? true : false)); }

void getscrext()
{
    switch(screenshottype)
    {
        case 2: result(".png"); break;
        case 1: result(".jpg"); break;
        case 0:
        default: result(".bmp"); break;
    }
}

COMMANDN(millis, millis_, ARG_IVAL);
COMMANDN(strlen, strlen_, ARG_1STR);
COMMANDN(substr, substr_, ARG_3STR);
COMMANDN(strpos, strpos_, ARG_3STR);
COMMAND(l0, ARG_2INT);
COMMAND(systime, ARG_NONE);
COMMANDN(timestamp, timestamp_, ARG_NONE);
COMMAND(datestring, ARG_NONE);
COMMANDN(timestring, timestring_, ARG_NONE);
COMMANDN(getmode, getmode_, ARG_1INT);
COMMAND(getscrext, ARG_NONE);

const char *currentserver(int i) // [client version]
{
    static string curSRVinfo;
    // using the curpeer directly we can get the info of our currently connected server
    string r;
    r[0] = '\0';
    extern ENetPeer *curpeer;
    if(curpeer)
    {
        switch(i)
        {
            case 1: // IP
            {
                uchar *ip = (uchar *)&curpeer->address.host;
                formatstring(r)("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                break;
            }
            case 2: // HOST
            {
                char hn[1024];
                formatstring(r)("%s", (enet_address_get_host(&curpeer->address, hn, sizeof(hn))==0) ? hn : "unknown");
                break;
            }
            case 3: // PORT
            {
                formatstring(r)("%d", curpeer->address.port);
                break;
            }
            case 4: // STATE
            {
                const char *statenames[] =
                {
                    "disconnected",
                    "connecting",
                    "acknowledging connect",
                    "connection pending",
                    "connection succeeded",
                    "connected",
                    "disconnect later",
                    "disconnecting",
                    "acknowledging disconnect",
                    "zombie"
                };
                if(curpeer->state>=0 && curpeer->state<int(sizeof(statenames)/sizeof(statenames[0])))
                    copystring(r, statenames[curpeer->state]);
                break; // 5 == Connected (compare ../enet/include/enet/enet.h +165)
            }
            // CAUTION: the following are only filled if the serverbrowser was used or the scoreboard shown
            // SERVERNAME
            case 5: { serverinfo *si = getconnectedserverinfo(); if(si) copystring(r, si->name); break; }
            // DESCRIPTION (3)
            case 6: { serverinfo *si = getconnectedserverinfo(); if(si) copystring(r, si->sdesc); break; }
            case 7: { serverinfo *si = getconnectedserverinfo(); if(si) copystring(r, si->description); break; }
            // CAUTION: the following is only the last full-description _seen_ in the serverbrowser!
            case 8: { serverinfo *si = getconnectedserverinfo(); if(si) copystring(r, si->full); break; }
            // just IP & PORT as default response - always available, no lookup-delay either
             default:
            {
                uchar *ip = (uchar *)&curpeer->address.host;
                formatstring(r)("%d.%d.%d.%d %d", ip[0], ip[1], ip[2], ip[3], curpeer->address.port);
                break;
            }
        }
    }
    copystring(curSRVinfo, r);
    return curSRVinfo;
}

COMMANDF(curserver, ARG_1INT, (int i) { result(currentserver(i)); });
#endif

void debugargs(char **args, int numargs)
{
    printf("debugargs: ");
    loopi(numargs)
    {
        if(i) printf(", ");
        printf("\"%s\"", args[i]);
    }
    printf("\n");
}

COMMAND(debugargs, ARG_VARI);