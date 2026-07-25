#include "drivers/thermocouple.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    double minimum_c;
    double maximum_c;
    const double *coefficients;
    size_t coefficient_count;
    bool has_exponential;
    double exponential_a0;
    double exponential_a1;
    double exponential_a2;
} thermocouple_piece_t;

typedef struct {
    const char *name;
    const thermocouple_piece_t *pieces;
    size_t piece_count;
    double inverse_minimum_c;
    double inverse_maximum_c;
} thermocouple_definition_t;

/* NIST SRD 60 / Monograph 175 reference-function coefficients, E in mV. */
static const double g_b_0[] = {
     0.000000000000e+00, -0.246508183460e-03,  0.590404211710e-05,
    -0.132579316360e-08,  0.156682919010e-11, -0.169445292400e-14,
     0.629903470940e-18,
};
static const double g_b_1[] = {
    -0.389381686210e+01,  0.285717474700e-01, -0.848851047850e-04,
     0.157852801640e-06, -0.168353448640e-09,  0.111097940130e-12,
    -0.445154310330e-16,  0.989756408210e-20, -0.937913302890e-24,
};
static const thermocouple_piece_t g_b_pieces[] = {
    { 0.0, 630.615, g_b_0, sizeof(g_b_0) / sizeof(g_b_0[0]), false, 0.0, 0.0, 0.0 },
    { 630.615, 1820.0, g_b_1, sizeof(g_b_1) / sizeof(g_b_1[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_e_0[] = {
     0.000000000000e+00,  0.586655087080e-01,  0.454109771240e-04,
    -0.779980486860e-06, -0.258001608430e-07, -0.594525830570e-09,
    -0.932140586670e-11, -0.102876055340e-12, -0.803701236210e-15,
    -0.439794973910e-17, -0.164147763550e-19, -0.396736195160e-22,
    -0.558273287210e-25, -0.346578420130e-28,
};
static const double g_e_1[] = {
     0.000000000000e+00,  0.586655087100e-01,  0.450322755820e-04,
     0.289084072120e-07, -0.330568966520e-09,  0.650244032700e-12,
    -0.191974955040e-15, -0.125366004970e-17,  0.214892175690e-20,
    -0.143880417820e-23,  0.359608994810e-27,
};
static const thermocouple_piece_t g_e_pieces[] = {
    { -270.0, 0.0, g_e_0, sizeof(g_e_0) / sizeof(g_e_0[0]), false, 0.0, 0.0, 0.0 },
    { 0.0, 1000.0, g_e_1, sizeof(g_e_1) / sizeof(g_e_1[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_j_0[] = {
     0.000000000000e+00,  0.503811878150e-01,  0.304758369300e-04,
    -0.856810657200e-07,  0.132281952950e-09, -0.170529583370e-12,
     0.209480906970e-15, -0.125383953360e-18,  0.156317256970e-22,
};
static const double g_j_1[] = {
     0.296456256810e+03, -0.149761277860e+01,  0.317871039240e-02,
    -0.318476867010e-05,  0.157208190040e-08, -0.306913690560e-12,
};
static const thermocouple_piece_t g_j_pieces[] = {
    { -210.0, 760.0, g_j_0, sizeof(g_j_0) / sizeof(g_j_0[0]), false, 0.0, 0.0, 0.0 },
    { 760.0, 1200.0, g_j_1, sizeof(g_j_1) / sizeof(g_j_1[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_k_0[] = {
     0.000000000000e+00,  0.394501280250e-01,  0.236223735980e-04,
    -0.328589067840e-06, -0.499048287770e-08, -0.675090591730e-10,
    -0.574103274280e-12, -0.310888728940e-14, -0.104516093650e-16,
    -0.198892668780e-19, -0.163226974860e-22,
};
static const double g_k_1[] = {
    -0.176004136860e-01,  0.389212049750e-01,  0.185587700320e-04,
    -0.994575928740e-07,  0.318409457190e-09, -0.560728448890e-12,
     0.560750590590e-15, -0.320207200030e-18,  0.971511471520e-22,
    -0.121047212750e-25,
};
static const thermocouple_piece_t g_k_pieces[] = {
    { -270.0, 0.0, g_k_0, sizeof(g_k_0) / sizeof(g_k_0[0]), false, 0.0, 0.0, 0.0 },
    { 0.0, 1372.0, g_k_1, sizeof(g_k_1) / sizeof(g_k_1[0]), true,
      0.118597600000e+00, -0.118343200000e-03, 0.126968600000e+03 },
};

static const double g_n_0[] = {
     0.000000000000e+00,  0.261591059620e-01,  0.109574842280e-04,
    -0.938411115540e-07, -0.464120397590e-10, -0.263033577160e-11,
    -0.226534380030e-13, -0.760893007910e-16, -0.934196678350e-19,
};
static const double g_n_1[] = {
     0.000000000000e+00,  0.259293946010e-01,  0.157101418800e-04,
     0.438256272370e-07, -0.252611697940e-09,  0.643118193390e-12,
    -0.100634715190e-14,  0.997453389920e-18, -0.608632456070e-21,
     0.208492293390e-24, -0.306821961510e-28,
};
static const thermocouple_piece_t g_n_pieces[] = {
    { -270.0, 0.0, g_n_0, sizeof(g_n_0) / sizeof(g_n_0[0]), false, 0.0, 0.0, 0.0 },
    { 0.0, 1300.0, g_n_1, sizeof(g_n_1) / sizeof(g_n_1[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_r_0[] = {
     0.000000000000e+00,  0.528961729765e-02,  0.139166589782e-04,
    -0.238855693017e-07,  0.356916001063e-10, -0.462347666298e-13,
     0.500777441034e-16, -0.373105886191e-19,  0.157716482367e-22,
    -0.281038625251e-26,
};
static const double g_r_1[] = {
     0.295157925316e+01, -0.252061251332e-02,  0.159564501865e-04,
    -0.764085947576e-08,  0.205305291024e-11, -0.293359668173e-15,
};
static const double g_r_2[] = {
     0.152232118209e+03, -0.268819888545e+00,  0.171280280471e-03,
    -0.345895706453e-07, -0.934633971046e-14,
};
static const thermocouple_piece_t g_r_pieces[] = {
    { -50.0, 1064.18, g_r_0, sizeof(g_r_0) / sizeof(g_r_0[0]), false, 0.0, 0.0, 0.0 },
    { 1064.18, 1664.5, g_r_1, sizeof(g_r_1) / sizeof(g_r_1[0]), false, 0.0, 0.0, 0.0 },
    { 1664.5, 1768.1, g_r_2, sizeof(g_r_2) / sizeof(g_r_2[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_s_0[] = {
     0.000000000000e+00,  0.540313308631e-02,  0.125934289740e-04,
    -0.232477968689e-07,  0.322028823036e-10, -0.331465196389e-13,
     0.255744251786e-16, -0.125068871393e-19,  0.271443176145e-23,
};
static const double g_s_1[] = {
     0.132900444085e+01,  0.334509311344e-02,  0.654805192818e-05,
    -0.164856259209e-08,  0.129989605174e-13,
};
static const double g_s_2[] = {
     0.146628232636e+03, -0.258430516752e+00,  0.163693574641e-03,
    -0.330439046987e-07, -0.943223690612e-14,
};
static const thermocouple_piece_t g_s_pieces[] = {
    { -50.0, 1064.18, g_s_0, sizeof(g_s_0) / sizeof(g_s_0[0]), false, 0.0, 0.0, 0.0 },
    { 1064.18, 1664.5, g_s_1, sizeof(g_s_1) / sizeof(g_s_1[0]), false, 0.0, 0.0, 0.0 },
    { 1664.5, 1768.1, g_s_2, sizeof(g_s_2) / sizeof(g_s_2[0]), false, 0.0, 0.0, 0.0 },
};

static const double g_t_0[] = {
     0.000000000000e+00,  0.387481063640e-01,  0.441944343470e-04,
     0.118443231050e-06,  0.200329735540e-07,  0.901380195590e-09,
     0.226511565930e-10,  0.360711542050e-12,  0.384939398830e-14,
     0.282135219250e-16,  0.142515947790e-18,  0.487686622860e-21,
     0.107955392700e-23,  0.139450270620e-26,  0.797951539270e-30,
};
static const double g_t_1[] = {
     0.000000000000e+00,  0.387481063640e-01,  0.332922278800e-04,
     0.206182434040e-06, -0.218822568460e-08,  0.109968809280e-10,
    -0.308157587720e-13,  0.454791352900e-16, -0.275129016730e-19,
};
static const thermocouple_piece_t g_t_pieces[] = {
    { -270.0, 0.0, g_t_0, sizeof(g_t_0) / sizeof(g_t_0[0]), false, 0.0, 0.0, 0.0 },
    { 0.0, 400.0, g_t_1, sizeof(g_t_1) / sizeof(g_t_1[0]), false, 0.0, 0.0, 0.0 },
};

static const thermocouple_definition_t g_definitions[THERMOCOUPLE_TYPE_COUNT] = {
    [THERMOCOUPLE_TYPE_B] = { "B", g_b_pieces, sizeof(g_b_pieces) / sizeof(g_b_pieces[0]), 250.0, 1820.0 },
    [THERMOCOUPLE_TYPE_E] = { "E", g_e_pieces, sizeof(g_e_pieces) / sizeof(g_e_pieces[0]), -270.0, 1000.0 },
    [THERMOCOUPLE_TYPE_J] = { "J", g_j_pieces, sizeof(g_j_pieces) / sizeof(g_j_pieces[0]), -210.0, 1200.0 },
    [THERMOCOUPLE_TYPE_K] = { "K", g_k_pieces, sizeof(g_k_pieces) / sizeof(g_k_pieces[0]), -270.0, 1372.0 },
    [THERMOCOUPLE_TYPE_N] = { "N", g_n_pieces, sizeof(g_n_pieces) / sizeof(g_n_pieces[0]), -270.0, 1300.0 },
    [THERMOCOUPLE_TYPE_R] = { "R", g_r_pieces, sizeof(g_r_pieces) / sizeof(g_r_pieces[0]), -50.0, 1768.1 },
    [THERMOCOUPLE_TYPE_S] = { "S", g_s_pieces, sizeof(g_s_pieces) / sizeof(g_s_pieces[0]), -50.0, 1768.1 },
    [THERMOCOUPLE_TYPE_T] = { "T", g_t_pieces, sizeof(g_t_pieces) / sizeof(g_t_pieces[0]), -270.0, 400.0 },
};

static bool thermocouple_type_is_valid(thermocouple_type_t type)
{
    return type >= THERMOCOUPLE_TYPE_B && type < THERMOCOUPLE_TYPE_COUNT;
}

static double polynomial(const double *coefficients, size_t count, double x)
{
    double result = 0.0;
    while (count > 0u) {
        count--;
        result = result * x + coefficients[count];
    }
    return result;
}

const char *thermocouple_type_name(thermocouple_type_t type)
{
    return thermocouple_type_is_valid(type) ? g_definitions[type].name : "?";
}

bool thermocouple_type_parse(const char *text, thermocouple_type_t *type)
{
    if (!text || !type) {
        return false;
    }

    while (isspace((unsigned char)*text)) {
        text++;
    }

    size_t length = strlen(text);
    if (length >= 4u &&
        (text[0] == 't' || text[0] == 'T') &&
        (text[1] == 'y' || text[1] == 'Y') &&
        (text[2] == 'p' || text[2] == 'P') &&
        (text[3] == 'e' || text[3] == 'E')) {
        text += 4;
        if (*text == '-' || *text == '_' || isspace((unsigned char)*text)) {
            text++;
        }
    }
    if (*text == '\0') {
        return false;
    }

    char letter = (char)toupper((unsigned char)*text++);
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text != '\0') {
        return false;
    }

    for (thermocouple_type_t candidate = THERMOCOUPLE_TYPE_B;
         candidate < THERMOCOUPLE_TYPE_COUNT;
         candidate = (thermocouple_type_t)(candidate + 1)) {
        if (g_definitions[candidate].name[0] == letter) {
            *type = candidate;
            return true;
        }
    }
    return false;
}

bool thermocouple_emf_mv(thermocouple_type_t type,
                          double temperature_c,
                          double *emf_mv)
{
    if (!thermocouple_type_is_valid(type) || !emf_mv ||
        !isfinite(temperature_c)) {
        return false;
    }

    const thermocouple_definition_t *definition = &g_definitions[type];
    for (size_t i = 0u; i < definition->piece_count; i++) {
        const thermocouple_piece_t *piece = &definition->pieces[i];
        if (temperature_c < piece->minimum_c ||
            temperature_c > piece->maximum_c) {
            continue;
        }

        double result = polynomial(piece->coefficients,
                                   piece->coefficient_count,
                                   temperature_c);
        if (piece->has_exponential) {
            double delta = temperature_c - piece->exponential_a2;
            result += piece->exponential_a0 *
                      exp(piece->exponential_a1 * delta * delta);
        }
        *emf_mv = result;
        return isfinite(result);
    }
    return false;
}

bool thermocouple_compensated_temperature_c(thermocouple_type_t type,
                                              double measured_mv,
                                              double cold_junction_c,
                                              double *temperature_c,
                                              double *compensated_emf_mv)
{
    if (!thermocouple_type_is_valid(type) || !temperature_c ||
        !isfinite(measured_mv) || !isfinite(cold_junction_c)) {
        return false;
    }

    double cjc_emf_mv = 0.0;
    if (!thermocouple_emf_mv(type, cold_junction_c, &cjc_emf_mv)) {
        return false;
    }

    const thermocouple_definition_t *definition = &g_definitions[type];
    double target_mv = cjc_emf_mv + measured_mv;
    double low_c = definition->inverse_minimum_c;
    double high_c = definition->inverse_maximum_c;
    double low_mv = 0.0;
    double high_mv = 0.0;
    if (!thermocouple_emf_mv(type, low_c, &low_mv) ||
        !thermocouple_emf_mv(type, high_c, &high_mv) ||
        target_mv < low_mv || target_mv > high_mv) {
        return false;
    }

    /* Reference functions are monotonic over each type's supported inverse range. */
    for (unsigned iteration = 0u; iteration < 32u; iteration++) {
        double midpoint_c = (low_c + high_c) * 0.5;
        double midpoint_mv = 0.0;
        if (!thermocouple_emf_mv(type, midpoint_c, &midpoint_mv)) {
            return false;
        }
        if (midpoint_mv < target_mv) {
            low_c = midpoint_c;
        } else {
            high_c = midpoint_c;
        }
    }

    *temperature_c = (low_c + high_c) * 0.5;
    if (compensated_emf_mv) {
        *compensated_emf_mv = target_mv;
    }
    return isfinite(*temperature_c);
}
