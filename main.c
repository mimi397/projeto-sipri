#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>


#define MAX_PRODUTOS 200
#define MAX_NOME 80
#define MAX_DESC 512
#define MAX_INGR 100
#define BUF_SIZE 512

/* Códigos de cores ANSI */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

/* Nomes de arquivos */
#define ARQ_PRODUTOS "produtos.dat"
#define ARQ_PRODUTOS_TMP "produtos.tmp"
#define ARQ_PRODUTOS_BAK "produtos.bak"
#define ARQ_CONFIG "config.dat"
#define ARQ_CONFIG_TMP "config.tmp"
#define ARQ_CONFIG_BAK "config.bak"

/* Configurações globais de despesas fixas (mensais) */
struct Config {
    double gasto_agua;
    double gasto_luz;
    double gasto_gas;
    int producao_mensal_unidades;
} config;

/* Estrutura de produto */
struct Produto {
    char nome[MAX_NOME];
    int modo;
    double preco_custo;
    double investimento_total;
    int rendimento;
    char ingredientes_desc[MAX_DESC];
    double despesas_variaveis;
    int usar_mei_comercio;
    double imposto_percent;
    double taxa_cartao_percent;
    double lucro_produtor_percent;
    double custo_unitario;
    double preco_produtor;
};

/* ----- Prototypes ----- */
void imprimir_aviso(const char *msg);
void imprimir_erro(const char *msg);
void imprimir_sucesso(const char *msg);
void imprimir_valor(const char *label, double valor);
void imprimir_secao(const char *titulo);
void limpar_tela();
void pausar();
void lerLinha(char *buf, int n);
double coletarIngredientesText(char *descricao, int descSize, int *rendimento);
void calcularTudo(struct Produto *p);
int salvarConfigAtomic();
int carregarConfig();
int salvarProdutosAtomic(struct Produto produtos[], int qtd);
int carregarProdutos(struct Produto produtos[], int *qtd);
void configurarDespesasFixas();
void listarProdutos(struct Produto produtos[], int qtd);
void editarProduto(struct Produto produtos[], int qtd);
void excluirProduto(struct Produto produtos[], int *qtd);
void calculoRapido();
void menuPosCadastro(struct Produto produtos[], int *qtd, int idxRecente);
void cadastrarProduto(struct Produto produtos[], int *qtd);
void excluirProdutoIndex(struct Produto produtos[], int *qtd, int idx);
void validarPercentuaisProduto(struct Produto *p);
double clamp_double(double v, double lo, double hi);

/* ----- Funções de interface ----- */
void limpar_tela() {
    /* mais portátil que system("clear") */
    printf("\033[2J\033[H");
}

void pausar() {
    char tmp[BUF_SIZE];
    printf("\n%s%sPressione ENTER para continuar...%s", BOLD, CYAN, RESET);
    if (fgets(tmp, sizeof(tmp), stdin) == NULL) return;
}

void imprimir_linha(char c, int tamanho) {
    for (int i = 0; i < tamanho; i++) printf("%c", c);
    printf("\n");
}

void imprimir_cabecalho(const char *titulo) {
    limpar_tela();
    printf("%s%s", BOLD, CYAN);
    imprimir_linha('=', 70);
    printf("  %s\n", titulo);
    imprimir_linha('=', 70);
    printf("%s", RESET);
}

void imprimir_secao(const char *titulo) {
    printf("\n%s%s> %s%s\n", BOLD, YELLOW, titulo, RESET);
    imprimir_linha('-', 70);
}

void imprimir_valor(const char *label, double valor) {
    printf("%s%-30s:%s %sR$ %.2f%s\n", CYAN, label, RESET, GREEN, valor, RESET);
}

void imprimir_sucesso(const char *msg) {
    printf("\n%s%s✓ %s%s\n", BOLD, GREEN, msg, RESET);
}

void imprimir_erro(const char *msg) {
    printf("\n%s%s✗ %s%s\n", BOLD, RED, msg, RESET);
}

void imprimir_aviso(const char *msg) {
    printf("\n%s%s⚠ %s%s\n", BOLD, YELLOW, msg, RESET);
}

/* ----- Util ----- */
double clamp_double(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ----- Valida percentuais e evita soma >= 100 ----- */
void validarPercentuaisProduto(struct Produto *p) {
    int changed = 0;
    /* imposto e taxa individuais 0..99 */
    if (p->imposto_percent < 0.0) { p->imposto_percent = 0.0; changed = 1; }
    if (p->taxa_cartao_percent < 0.0) { p->taxa_cartao_percent = 0.0; changed = 1; }
    if (p->lucro_produtor_percent < 0.0) { p->lucro_produtor_percent = 0.0; changed = 1; }
    p->imposto_percent = clamp_double(p->imposto_percent, 0.0, 99.0);
    p->taxa_cartao_percent = clamp_double(p->taxa_cartao_percent, 0.0, 99.0);
    p->lucro_produtor_percent = clamp_double(p->lucro_produtor_percent, 0.0, 99.0);

    /* garantir que imposto + taxa < 99 (reservamos pelo menos 1% para dividir) */
    double total = p->imposto_percent + p->taxa_cartao_percent;
    if (total >= 99.0) {
        /* reduz proporcionalmente as duas para manter proporção */
        if (total > 0.0) {
            double factor = 98.0 / total; /* deixa 98% como soma */
            p->imposto_percent *= factor;
            p->taxa_cartao_percent *= factor;
        } else {
            p->imposto_percent = 0.0;
            p->taxa_cartao_percent = 0.0;
        }
        changed = 1;
    }

    if (changed) {
        imprimir_aviso("Alguns percentuais foram ajustados para valores validos (0-99%% e imposto+taxa < 99%%).");
    }
}

/* ----- Funções de arquivo (atômico com temp + rename + backup) ----- */
int salvarConfigAtomic() {
    /* escreve temporário */
    FILE *f = fopen(ARQ_CONFIG_TMP, "wb");
    if (!f) return 0;
    if (fwrite(&config, sizeof(struct Config), 1, f) != 1) {
        fclose(f);
        remove(ARQ_CONFIG_TMP);
        return 0;
    }
    fflush(f);
    fclose(f);

    /* move antigo para backup se existir */
    if (access(ARQ_CONFIG, F_OK) == 0) {
        /* remove antigo backup se houver */
        remove(ARQ_CONFIG_BAK);
        if (rename(ARQ_CONFIG, ARQ_CONFIG_BAK) != 0) {
            /* se falha, tenta remover tmp e retorna erro */
            remove(ARQ_CONFIG_TMP);
            return 0;
        }
    }

    if (rename(ARQ_CONFIG_TMP, ARQ_CONFIG) != 0) {
        /* tenta restaurar backup */
        if (access(ARQ_CONFIG_BAK, F_OK) == 0) {
            rename(ARQ_CONFIG_BAK, ARQ_CONFIG);
        }
        remove(ARQ_CONFIG_TMP);
        return 0;
    }

    /* sucesso */
    return 1;
}

int carregarConfig() {
    FILE *f = fopen(ARQ_CONFIG, "rb");
    if (!f) return 0;
    size_t r = fread(&config, sizeof(struct Config), 1, f);
    fclose(f);
    return r == 1;
}

int salvarProdutosAtomic(struct Produto produtos[], int qtd) {
    FILE *f = fopen(ARQ_PRODUTOS_TMP, "wb");
    if (!f) return 0;
    for (int i = 0; i < qtd; i++) {
        if (fwrite(&produtos[i], sizeof(struct Produto), 1, f) != 1) {
            fclose(f);
            remove(ARQ_PRODUTOS_TMP);
            return 0;
        }
    }
    fflush(f);
    fclose(f);

    /* backup do antigo */
    if (access(ARQ_PRODUTOS, F_OK) == 0) {
        remove(ARQ_PRODUTOS_BAK);
        if (rename(ARQ_PRODUTOS, ARQ_PRODUTOS_BAK) != 0) {
            /* falha em renomear antigo -> tenta remover tmp e retorna erro */
            remove(ARQ_PRODUTOS_TMP);
            return 0;
        }
    }

    if (rename(ARQ_PRODUTOS_TMP, ARQ_PRODUTOS) != 0) {
        /* se falhar, tenta restaurar backup */
        if (access(ARQ_PRODUTOS_BAK, F_OK) == 0) {
            rename(ARQ_PRODUTOS_BAK, ARQ_PRODUTOS);
        }
        remove(ARQ_PRODUTOS_TMP);
        return 0;
    }

    return 1;
}

int carregarProdutos(struct Produto produtos[], int *qtd) {
    FILE *f = fopen(ARQ_PRODUTOS, "rb");
    if (!f) { *qtd = 0; return 0; }
    *qtd = 0;
    while (fread(&produtos[*qtd], sizeof(struct Produto), 1, f) == 1) {
        (*qtd)++;
        if (*qtd >= MAX_PRODUTOS) break;
    }
    fclose(f);
    return 1;
}

/* ----- Auxiliares I/O ----- */
void lerLinha(char *buf, int n) {
    if (fgets(buf, n, stdin) == NULL) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\n")] = '\0';
}

/* ----- Coleta de ingredientes (modo receita) ----- */
double coletarIngredientesText(char *descricao, int descSize, int *rendimento) {
    char buf[BUF_SIZE];
    int n;
    double custo_total = 0.0;

    descricao[0] = '\0';

    printf("\n%sQuantos ingredientes tem essa receita? %s", YELLOW, RESET);
    lerLinha(buf, sizeof(buf));
    n = atoi(buf);
    if (n < 1) return -1.0;
    if (n > MAX_INGR) n = MAX_INGR;

    for (int i = 0; i < n; i++) {
        char nome[128];
        int tipo;
        double preco, quantidade, custo;

        printf("\n%s%s> Ingrediente %d%s\n", BOLD, MAGENTA, i + 1, RESET);

        printf("%sNome: %s", CYAN, RESET);
        lerLinha(nome, sizeof(nome));

        printf("%sTipo (1=preco/kg | 2=preco por unidade): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        tipo = atoi(buf);
        if (tipo != 1 && tipo != 2) tipo = 1;

        if (tipo == 2) {
            printf("%sPreco por unidade (R$): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            preco = atof(buf);
            printf("%sQuantidade usada (unidades): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            quantidade = atof(buf);
            custo = preco * quantidade;
            snprintf(buf, sizeof(buf), "  • %s: %.0f un x R$ %.2f = R$ %.2f\n", nome, quantidade, preco, custo);
        } else {
            printf("%sPreco por KG (R$): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            preco = atof(buf);
            printf("%sQuantidade usada (gramas): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            quantidade = atof(buf);
            custo = (preco / 1000.0) * quantidade;
            snprintf(buf, sizeof(buf), "  • %s: %.0fg x R$ %.2f/kg = R$ %.2f\n", nome, quantidade, preco, custo);
        }

        custo_total += custo;
        if ((int)strlen(descricao) + (int)strlen(buf) < descSize - 1)
            strncat(descricao, buf, descSize - strlen(descricao) - 1);
        printf("%s-> Custo de %s: %sR$ %.2f%s\n", GREEN, nome, BOLD, custo, RESET);
    }

    printf("\n%sRendimento da receita (quantas unidades produz): %s", YELLOW, RESET);
    lerLinha(buf, sizeof(buf));
    *rendimento = atoi(buf);
    if (*rendimento <= 0) *rendimento = 1;

    return custo_total;
}

/* ----- Cálculo completo por produto ----- */
void calcularTudo(struct Produto *p) {
    double custo_base_unitario;

    if (p->modo == 1) {
        custo_base_unitario = p->preco_custo;
    } else {
        if (p->rendimento <= 0) p->rendimento = 1;
        double despesasVariaveisPorUn = p->despesas_variaveis / (double)p->rendimento;
        custo_base_unitario = (p->investimento_total / (double)p->rendimento) + despesasVariaveisPorUn;
    }

    double total_fixa = config.gasto_agua + config.gasto_luz + config.gasto_gas;
    double rateio_fixo_por_unidade = 0.0;
    if (config.producao_mensal_unidades > 0) {
        rateio_fixo_por_unidade = total_fixa / (double)config.producao_mensal_unidades;
    }

    p->custo_unitario = custo_base_unitario + rateio_fixo_por_unidade;

    if (p->usar_mei_comercio) p->imposto_percent = 4.0;

    /* garantir percentuais válidos antes do cálculo */
    validarPercentuaisProduto(p);

    double lucro_valor = p->custo_unitario * (p->lucro_produtor_percent / 100.0);
    double preco_com_lucro = p->custo_unitario + lucro_valor;

    double total_percent = p->imposto_percent + p->taxa_cartao_percent;
    if (total_percent >= 100.0) total_percent = 99.0;

    p->preco_produtor = preco_com_lucro / (1.0 - (total_percent / 100.0));
}

/* ----- Configurar despesas fixas globais ----- */
void configurarDespesasFixas() {
    char buf[BUF_SIZE];
    imprimir_cabecalho("CONFIGURAR DESPESAS FIXAS MENSAIS");

    printf("\n%s%sVALORES ATUAIS:%s\n", BOLD, YELLOW, RESET);
    imprimir_valor("Agua", config.gasto_agua);
    imprimir_valor("Luz", config.gasto_luz);
    imprimir_valor("Gas", config.gasto_gas);
    printf("%sProducao mensal          :%s %d unidades\n\n", CYAN, RESET, config.producao_mensal_unidades);

    printf("%sInforme gasto mensal com AGUA (R$): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') config.gasto_agua = atof(buf);

    printf("%sInforme gasto mensal com LUZ (R$): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') config.gasto_luz = atof(buf);

    printf("%sInforme gasto mensal com GAS (R$): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') config.gasto_gas = atof(buf);

    printf("%sInforme a PRODUCAO MENSAL (unidades/mes): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') config.producao_mensal_unidades = atoi(buf);

    if (!salvarConfigAtomic()) {
        imprimir_aviso("Falha ao salvar configuracoes em disco.");
    } else {
        imprimir_sucesso("Configuracoes atualizadas com sucesso!");
    }
    pausar();
}

/* ----- Listar produtos ----- */
void listarProdutos(struct Produto produtos[], int qtd) {
    imprimir_cabecalho("LISTA DE PRODUTOS CADASTRADOS");

    if (qtd == 0) {
        imprimir_aviso("Nenhum produto cadastrado ainda.");
        pausar();
        return;
    }

    for (int i = 0; i < qtd; i++) {
        struct Produto *p = &produtos[i];

        printf("\n%s%s+--- PRODUTO #%d --------------------------------------------------+%s\n", BOLD, BLUE, i+1, RESET);
        printf("%s|%s %s%-60s%s\n", BLUE, RESET, BOLD, p->nome, RESET);
        printf("%s+-------------------------------------------------------------------+%s\n", BLUE, RESET);

        if (p->modo == 1) {
            printf("%sModo                         :%s Custo direto por unidade\n", CYAN, RESET);
            imprimir_valor("Custo informado/unidade", p->preco_custo);
        } else {
            printf("%sModo                         :%s Receita (ingredientes)\n", CYAN, RESET);
            imprimir_valor("Investimento total", p->investimento_total);
            printf("%sRendimento                   :%s %d unidades\n", CYAN, RESET, p->rendimento);
            printf("\n%s  Ingredientes:%s\n%s", YELLOW, RESET, p->ingredientes_desc);
            imprimir_valor("Despesas variaveis", p->despesas_variaveis);
        }

        double rateio = (config.producao_mensal_unidades > 0) ?
            (config.gasto_agua + config.gasto_luz + config.gasto_gas) / config.producao_mensal_unidades : 0.0;
        imprimir_valor("Rateio despesas fixas/un", rateio);
        imprimir_valor("CUSTO UNITARIO FINAL", p->custo_unitario);

        printf("\n%s  Configuracoes financeiras:%s\n", YELLOW, RESET);
        printf("%sImposto                      :%s %.2f%% %s\n",
               CYAN, RESET, p->imposto_percent, p->usar_mei_comercio ? "(MEI Comercio)" : "");
        printf("%sTaxa cartao                  :%s %.2f%%\n", CYAN, RESET, p->taxa_cartao_percent);
        printf("%sLucro desejado               :%s %.2f%%\n", CYAN, RESET, p->lucro_produtor_percent);

        printf("\n%s%s> PRECO FINAL SUGERIDO: R$ %.2f%s\n", BOLD, GREEN, p->preco_produtor, RESET);
        imprimir_linha('-', 70);
    }

    pausar();
}

/* ----- Editar produto ----- */
void editarProduto(struct Produto produtos[], int qtd) {
    if (qtd == 0) {
        imprimir_aviso("Nenhum produto para editar.");
        pausar();
        return;
    }

    listarProdutos(produtos, qtd);

    char buf[BUF_SIZE];
    printf("\n%sNumero do produto para editar: %s", YELLOW, RESET);
    lerLinha(buf, sizeof(buf));
    int idx = atoi(buf) - 1;

    if (idx < 0 || idx >= qtd) {
        imprimir_erro("Numero invalido!");
        pausar();
        return;
    }

    struct Produto *p = &produtos[idx];
    imprimir_cabecalho("EDITAR PRODUTO");

    printf("%sNovo nome [Enter mantem: %s]: %s", CYAN, p->nome, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') {
        strncpy(p->nome, buf, sizeof(p->nome)-1);
        p->nome[sizeof(p->nome)-1] = '\0';
    }

    printf("%sAlterar modo/ingredientes? (s/n): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == 's' || buf[0] == 'S') {
        printf("%sNovo modo (1=custo direto | 2=receita): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p->modo = atoi(buf);
        if (p->modo == 1) {
            printf("%sPreco de custo/un (R$): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            p->preco_custo = atof(buf);
            p->investimento_total = p->preco_custo;
            p->rendimento = 1;
            p->ingredientes_desc[0] = '\0';
            p->despesas_variaveis = 0.0;
        } else {
            double custoCalc = coletarIngredientesText(p->ingredientes_desc, sizeof(p->ingredientes_desc), &p->rendimento);
            if (custoCalc >= 0.0) p->investimento_total = custoCalc;
            printf("%sDespesas variaveis (R$) [Enter mantem %.2f]: %s", CYAN, p->despesas_variaveis, RESET);
            lerLinha(buf, sizeof(buf));
            if (buf[0] != '\0') p->despesas_variaveis = atof(buf);
        }
    }

    printf("%sUsar MEI comercio? (s/n) [atual: %s]: %s", CYAN, p->usar_mei_comercio ? "sim" : "nao", RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == 's' || buf[0] == 'S') {
        p->usar_mei_comercio = 1;
        p->imposto_percent = 4.0;
    } else {
        p->usar_mei_comercio = 0;
        printf("%sImposto (%%) [atual %.2f, Enter mantem]: %s", CYAN, p->imposto_percent, RESET);
        lerLinha(buf, sizeof(buf));
        if (buf[0] != '\0') p->imposto_percent = atof(buf);
    }

    printf("%sTaxa cartao [atual %.2f, Enter mantem]: %s", CYAN, p->taxa_cartao_percent, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') p->taxa_cartao_percent = atof(buf);

    printf("%sLucro produtor [atual %.2f, Enter mantem]: %s", CYAN, p->lucro_produtor_percent, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != '\0') p->lucro_produtor_percent = atof(buf);

    /* validar e recalcular */
    validarPercentuaisProduto(p);
    calcularTudo(p);
    if (!salvarProdutosAtomic(produtos, qtd))
        imprimir_aviso("Falha ao salvar apos edicao.");

    imprimir_sucesso("Produto atualizado e recalculado!");
    pausar();
}

/* ----- Excluir produto ----- */
void excluirProdutoIndex(struct Produto produtos[], int *qtd, int idx) {
    if (idx < 0 || idx >= *qtd) {
        imprimir_erro("Indice invalido para exclusao.");
        return;
    }
    for (int i = idx; i < (*qtd) - 1; i++)
        produtos[i] = produtos[i + 1];
    (*qtd)--;
}

void excluirProduto(struct Produto produtos[], int *qtd) {
    if (*qtd == 0) {
        imprimir_aviso("Nenhum produto para excluir.");
        pausar();
        return;
    }

    listarProdutos(produtos, *qtd);

    char buf[BUF_SIZE];
    printf("\n%s%sNumero do produto para EXCLUIR: %s", BOLD, RED, RESET);
    lerLinha(buf, sizeof(buf));
    int idx = atoi(buf) - 1;

    if (idx < 0 || idx >= *qtd) {
        imprimir_erro("Numero invalido!");
        pausar();
        return;
    }

    printf("%s%sTem certeza? (s/n): %s", BOLD, RED, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] != 's' && buf[0] != 'S') {
        printf("Operacao cancelada.\n");
        pausar();
        return;
    }

    excluirProdutoIndex(produtos, qtd, idx);
    if (!salvarProdutosAtomic(produtos, *qtd))
        imprimir_aviso("Falha ao salvar apos exclusao.");

    imprimir_sucesso("Produto excluido!");
    pausar();
}

/* ----- Cálculo rápido ----- */
void calculoRapido() {
    imprimir_cabecalho("CALCULO RAPIDO");

    char buf[BUF_SIZE];
    struct Produto p;
    memset(&p, 0, sizeof(p));

    printf("%sModo (1=custo direto | 2=receita): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.modo = atoi(buf);
    if (p.modo != 1 && p.modo != 2) p.modo = 1;

    if (p.modo == 1) {
        printf("%sPreco de custo/un (R$): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.preco_custo = atof(buf);
    } else {
        double c = coletarIngredientesText(p.ingredientes_desc, sizeof(p.ingredientes_desc), &p.rendimento);
        if (c < 0.0) return;
        p.investimento_total = c;
        printf("%sDespesas variaveis (R$) [Enter=0]: %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        if (buf[0] != '\0') p.despesas_variaveis = atof(buf);
    }

    printf("%sUsar MEI comercio (4%%)? (s/n): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == 's' || buf[0] == 'S') {
        p.usar_mei_comercio = 1;
        p.imposto_percent = 4.0;
    } else {
        printf("%sImposto (%%): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.imposto_percent = atof(buf);
    }

    printf("%sTaxa cartao (%%): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.taxa_cartao_percent = atof(buf);

    printf("%sLucro desejado (%%): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.lucro_produtor_percent = atof(buf);

    validarPercentuaisProduto(&p);
    calcularTudo(&p);

    imprimir_secao("RESULTADO");
    imprimir_valor("Custo unitario (com rateio)", p.custo_unitario);
    printf("%s%s> PRECO FINAL SUGERIDO: R$ %.2f%s\n", BOLD, GREEN, p.preco_produtor, RESET);

    pausar();
}

/* ----- Menu curto após cadastro ----- */
void menuPosCadastro(struct Produto produtos[], int *qtd, int idxRecente) {
    char buf[BUF_SIZE];
    int opc = 0;
    while (1) {
        imprimir_secao("O QUE DESEJA FAZER AGORA?");
        printf("%s1%s - Salvar agora\n", GREEN, RESET);
        printf("%s2%s - Editar este produto\n", GREEN, RESET);
        printf("%s3%s - Excluir este produto\n", GREEN, RESET);
        printf("%s4%s - Listar produtos\n", GREEN, RESET);
        printf("%s5%s - Voltar ao menu principal\n", YELLOW, RESET);

        printf("\n%sOpcao: %s", BOLD, RESET);
        lerLinha(buf, sizeof(buf));
        opc = atoi(buf);

        if (opc == 1) {
            if (salvarProdutosAtomic(produtos, *qtd))
                imprimir_sucesso("Produtos salvos!");
            else
                imprimir_erro("Falha ao salvar!");
            pausar();
        } else if (opc == 2) {
            if (idxRecente >= 0 && idxRecente < *qtd) {
                editarProduto(produtos, *qtd);
            } else {
                imprimir_erro("Indice do produto invalido para edicao.");
                pausar();
            }
            break;
        } else if (opc == 3) {
            if (idxRecente >= 0 && idxRecente < *qtd) {
                printf("%s%sTem certeza que deseja excluir o produto criado? (s/n): %s", BOLD, RED, RESET);
                lerLinha(buf, sizeof(buf));
                if (buf[0] == 's' || buf[0] == 'S') {
                    excluirProdutoIndex(produtos, qtd, idxRecente);
                    if (!salvarProdutosAtomic(produtos, *qtd))
                        imprimir_aviso("Falha ao salvar apos exclusao.");
                    imprimir_sucesso("Produto excluido!");
                } else {
                    printf("Operacao cancelada.\n");
                }
                pausar();
            } else {
                imprimir_erro("Indice do produto invalido para exclusao.");
                pausar();
            }
            break;
        } else if (opc == 4) {
            listarProdutos(produtos, *qtd);
        } else if (opc == 5) {
            break;
        } else {
            imprimir_erro("Opcao invalida!");
            pausar();
        }
    }
}

/* ----- Cadastro de produto ----- */
void cadastrarProduto(struct Produto produtos[], int *qtd) {
    if (*qtd >= MAX_PRODUTOS) {
        imprimir_erro("Limite de produtos atingido!");
        pausar();
        return;
    }

    imprimir_cabecalho("CADASTRAR NOVO PRODUTO");

    char buf[BUF_SIZE];
    struct Produto p;
    memset(&p, 0, sizeof(p));

    printf("\n%sNome do produto: %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == '\0') {
        imprimir_aviso("Nome vazio — atribuindo nome padrao.");
        snprintf(p.nome, sizeof(p.nome), "Produto %d", (*qtd) + 1);
    } else {
        strncpy(p.nome, buf, sizeof(p.nome)-1);
        p.nome[sizeof(p.nome)-1] = '\0';
    }

    printf("%sModo (1=custo direto/unidade | 2=receita com ingredientes): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.modo = atoi(buf);
    if (p.modo != 1 && p.modo != 2) p.modo = 1;

    if (p.modo == 1) {
        printf("%sPreco de custo por unidade (R$): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.preco_custo = atof(buf);
    } else {
        imprimir_secao("INGREDIENTES DA RECEITA");
        double custoCalc = coletarIngredientesText(p.ingredientes_desc, sizeof(p.ingredientes_desc), &p.rendimento);
        if (custoCalc < 0.0) {
            imprimir_erro("Erro na insercao de ingredientes. Cadastro cancelado.");
            pausar();
            return;
        }
        p.investimento_total = custoCalc;

        printf("\n%sDespesas variaveis (embalagem, entrega etc.) - R$ [Enter=0]: %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        if (buf[0] != '\0') p.despesas_variaveis = atof(buf);
        else p.despesas_variaveis = 0.0;
    }

    imprimir_secao("CONFIGURACOES FINANCEIRAS");

    printf("%sUsar regime MEI COMERCIO (imposto 4%%)? (s/n): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == 's' || buf[0] == 'S') {
        p.usar_mei_comercio = 1;
        p.imposto_percent = 4.0;
        printf("%s✓ MEI Comercio aplicado (4%%)%s\n", GREEN, RESET);
    } else {
        p.usar_mei_comercio = 0;
        printf("%sPercentual de imposto (%%): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.imposto_percent = atof(buf);
    }

    printf("%sTaxa da maquininha/cartao (%%) [ex: 2.5]: %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.taxa_cartao_percent = atof(buf);

    printf("%sPercentual de lucro desejado (%%): %s", CYAN, RESET);
    lerLinha(buf, sizeof(buf));
    p.lucro_produtor_percent = atof(buf);

    /* valida percentuais antes de calcular */
    validarPercentuaisProduto(&p);

    calcularTudo(&p);

    /* garantir nome terminado e seguro já foi feito */
    produtos[*qtd] = p;
    int idxRecente = *qtd;
    (*qtd)++;

    /* salva em disco automaticamente e cria backup */
    if (!salvarProdutosAtomic(produtos, *qtd)) {
        imprimir_aviso("Falha ao salvar arquivo (produto ficou em memoria)");
    }

    imprimir_secao("RESULTADO DO CADASTRO");
    imprimir_sucesso("Produto cadastrado com sucesso!");
    imprimir_valor("Custo unitario (com rateio)", p.custo_unitario);
    imprimir_valor("PRECO FINAL AO CLIENTE", p.preco_produtor);

    /* menu curto pós-cadastro */
    menuPosCadastro(produtos, qtd, idxRecente);
}

/* ----- Menu principal ----- */
int main() {
    struct Produto produtos[MAX_PRODUTOS];
    int qtd = 0;

    /* valores default */
    config.gasto_agua = 0.0;
    config.gasto_luz = 0.0;
    config.gasto_gas = 0.0;
    config.producao_mensal_unidades = 0;

    /* carregar config e produtos ao iniciar */
    if (!carregarConfig()) {
        /* não existe config.dat: mantém defaults e tenta salvar (não crítico) */
        if (!salvarConfigAtomic()) {
            imprimir_aviso("Nao foi possivel criar config.dat com valores default.");
        }
    }

    carregarProdutos(produtos, &qtd);

    char buf[BUF_SIZE];
    int opc;

    do {
        imprimir_cabecalho("SIPRI - SISTEMA DE PRECIFICACAO INTELIGENTE");

        printf("\n%s%sCONFIGURACOES ATUAIS:%s\n", BOLD, MAGENTA, RESET);
        printf("%s+-%s Agua: R$ %.2f/mes\n", CYAN, RESET, config.gasto_agua);
        printf("%s+-%s Luz:  R$ %.2f/mes\n", CYAN, RESET, config.gasto_luz);
        printf("%s+-%s Gas:  R$ %.2f/mes\n", CYAN, RESET, config.gasto_gas);
        printf("%s+-%s Producao mensal: %d unidades\n", CYAN, RESET, config.producao_mensal_unidades);

        printf("\n%s%sMENU PRINCIPAL:%s\n", BOLD, YELLOW, RESET);
        printf("%s1%s - Cadastrar produto\n", GREEN, RESET);
        printf("%s2%s - Listar produtos\n", GREEN, RESET);
        printf("%s3%s - Editar produto\n", GREEN, RESET);
        printf("%s4%s - Excluir produto\n", GREEN, RESET);
        printf("%s5%s - Calculo rapido\n", GREEN, RESET);
        printf("%s6%s - Configurar despesas fixas\n", GREEN, RESET);
        printf("%s7%s - Salvar produtos\n", GREEN, RESET);
        printf("%s8%s - Carregar produtos\n", GREEN, RESET);
        printf("%s9%s - Sair\n", RED, RESET);

        printf("\n%s%sOpcao: %s", BOLD, CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        opc = atoi(buf);

        switch (opc) {
            case 1: cadastrarProduto(produtos, &qtd); break;
            case 2: listarProdutos(produtos, qtd); break;
            case 3: editarProduto(produtos, qtd); break;
            case 4: excluirProduto(produtos, &qtd); break;
            case 5: calculoRapido(); break;
            case 6: configurarDespesasFixas(); break;
            case 7:
                if (salvarProdutosAtomic(produtos, qtd))
                    imprimir_sucesso("Produtos salvos!");
                else
                    imprimir_erro("Falha ao salvar!");
                pausar();
                break;
            case 8:
                carregarProdutos(produtos, &qtd);
                imprimir_sucesso("Produtos carregados!");
                printf("Total de produtos: %d\n", qtd);
                pausar();
                break;
            case 9:
                limpar_tela();
                printf("\n%s%sObrigado por usar o SIPRI!%s\n\n", BOLD, GREEN, RESET);
                break;
            default:
                imprimir_erro("Opcao invalida!");
                pausar();
        }
    } while (opc != 9);

    return 0;
}
