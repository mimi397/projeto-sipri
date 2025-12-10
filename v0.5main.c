#include <stdio.h>      // printf, scanf, FILE
#include <stdlib.h>     // malloc, free, exit
#include <string.h>     // strcpy, strcmp, strlen
#include <errno.h>      // erros de I/O
#include <unistd.h>     // unlink(), sleep(), access()

#define MAX_PRODUTOS 200  // quantidade máxima de produtos
#define MAX_NOME 80       // tamanho máximo do nome
#define MAX_DESC 512      // tamanho da descrição
#define MAX_INGR 100      // número máx de ingredientes
#define BUF_SIZE 512      // buffer genérico para leitura segura
#define MAX_PERCENT 99.0
#define HALF_MAX_PERCENT 49.5

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
#define ARQ_PRODUTOS "produtos.dat"      // base de dados principal
#define ARQ_PRODUTOS_TMP "produtos.tmp"  // arquivo temporário para escrita
#define ARQ_PRODUTOS_BAK "produtos.bak"  // backup automático
#define ARQ_CONFIG "config.dat"          // base de dados principal
#define ARQ_CONFIG_TMP "config.tmp"      // arquivo temporário para escrita
#define ARQ_CONFIG_BAK "config.bak"      // backup automático

/* Configurações globais de despesas fixas (mensais) */
struct Config {
    double gasto_agua;
    double gasto_luz;
    double gasto_gas;
    int producao_mensal_unidades;
} config;

struct Produto {
    char nome[MAX_NOME];       // Nome do produto
    int modo;                  // Tipo de cálculo

    double preco_custo;        // Custo total da receita
    double investimento_total; // Investimento inicial
    int rendimento;            // Quantas unidades rende
    char ingredientes_desc[MAX_DESC]; // Descrição textual

    /* Despesas e impostos */
    double despesas_variaveis;     // Custos variáveis
    int usar_mei_comercio;         // MEI? (0/1)
    double imposto_percent;        // Percentual de imposto (0.00 - 99.00)
    double taxa_cartao_percent;    // Percentual da taxa de cartão

    /* Margens e preços */
    double lucro_produtor_percent; // Percentual de lucro desejado
    double custo_unitario;         // Valor final por unidade
    double preco_produtor;         // Preço de venda
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
void editarProduto(struct Produto produtos[], int qtd);
void excluirProdutoIndex(struct Produto produtos[], int *qtd, int idx);
void listarProdutos(struct Produto produtos[], int qtd);
void excluirProduto(struct Produto produtos[], int *qtd);
void calculoRapido();
double lerDoubleValidado(const char *msg, double valorAtual);
int lerIntValidado(const char *msg, int valorAtual);
struct Produto validarPercentuaisProduto(struct Produto p);
void menuGerenciarProdutos(struct Produto produtos[], int *qtd);
void cadastrarProduto(struct Produto produtos[], int *qtd);

//Funções de interface
void limpar_tela() {
    #ifdef _WIN32
        system("cls");
    #else
        printf("\033[2J\033[H");
    #endif
}

void pausar() {
    char tmp[BUF_SIZE];
    printf("\n%s%sPressione ENTER para continuar...%s", BOLD, CYAN, RESET);
    if (fgets(tmp, sizeof(tmp), stdin) == NULL) return;
}

void imprimir_linha(char c, int tamanho) {
    for (int i = 0; i < tamanho; i++)
        printf("%c", c);
    printf("\n");
}

void imprimir_cabecalho(const char *titulo) {
    limpar_tela();
    printf("%s%s", BOLD, CYAN);
    imprimir_linha('=', 70);
    int len = strlen(titulo);
    int espacos = (70 - len) / 2;
    for (int i = 0; i < espacos; i++)
        printf(" ");
    printf("%s\n", titulo);
    imprimir_linha('=', 70);
    printf("%s", RESET);
}

void imprimir_secao(const char *titulo) {
    printf("\n%s%s%s%s\n", BOLD, YELLOW, titulo, RESET);
    imprimir_linha('-', 70);
}

void imprimir_valor(const char *label, double valor) {
    printf("%-30s: R$ %10.2f\n", label, valor);
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

double clamp_double(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

struct Produto validarPercentuaisProduto(struct Produto p)
{
    int alterado = 0;

    if (p.imposto_percent < 0) { p.imposto_percent = 0; alterado = 1; }
    if (p.taxa_cartao_percent < 0) { p.taxa_cartao_percent = 0; alterado = 1; }
    if (p.lucro_produtor_percent < 0) { p.lucro_produtor_percent = 0; alterado = 1; }

    if (p.imposto_percent > 99) { p.imposto_percent = 99; alterado = 1; }
    if (p.taxa_cartao_percent > 99) { p.taxa_cartao_percent = 99; alterado = 1; }
    if (p.lucro_produtor_percent > 99) { p.lucro_produtor_percent = 99; alterado = 1; }

    double soma = p.imposto_percent + p.taxa_cartao_percent;
    if (soma > 99) {
        p.imposto_percent = 49.5;
        p.taxa_cartao_percent = 49.5;
        alterado = 1;
    }

    if (alterado) {
        imprimir_aviso("Percentuais ajustados para valores validos.");
    }
    return p;
}

int salvarConfigAtomic() {
    FILE *f = fopen(ARQ_CONFIG_TMP, "wb");
    if (!f) {
        imprimir_erro("Nao foi possivel criar arquivo temporario.");
        return 0;
    }
    if (fwrite(&config, sizeof(config), 1, f) != 1) {
        imprimir_erro("Falha ao gravar configuracao.");
        fclose(f);
        remove(ARQ_CONFIG_TMP);
        return 0;
    }
    fclose(f);
    remove(ARQ_CONFIG_BAK);
    if (rename(ARQ_CONFIG, ARQ_CONFIG_BAK) != 0) {
    }
    if (rename(ARQ_CONFIG_TMP, ARQ_CONFIG) != 0) {
        imprimir_erro("Falha ao salvar configuracao!");
        rename(ARQ_CONFIG_BAK, ARQ_CONFIG);
        remove(ARQ_CONFIG_TMP);
        return 0;
    }
    imprimir_sucesso("Configuracao salva com sucesso!");
    return 1;
}

int carregarConfig() {
    FILE *f = fopen(ARQ_CONFIG, "rb");
    if (!f) return 0;
    if (fread(&config, sizeof(config), 1, f) != 1) {
        imprimir_erro("Erro ao ler configuracao.");
        fclose(f);
        return 0;
    }
    fclose(f);
    imprimir_sucesso("Configuracao carregada!");
    return 1;
}

int salvarProdutosAtomic(struct Produto produtos[], int qtd) {
    FILE *f = fopen(ARQ_PRODUTOS_TMP, "wb");
    if (!f) {
        printf("Erro: não foi possível criar arquivo temporário.\n");
        return 0;
    }
    for (int i = 0; i < qtd; i++) {
        if (fwrite(&produtos[i], sizeof(struct Produto), 1, f) != 1) {
            printf("Erro: falha ao salvar dados no arquivo temporário.\n");
            fclose(f);
            remove(ARQ_PRODUTOS_TMP);
            return 0;
        }
    }
    fclose(f);
    if (access(ARQ_PRODUTOS, F_OK) == 0) {
        remove(ARQ_PRODUTOS_BAK);
        if (rename(ARQ_PRODUTOS, ARQ_PRODUTOS_BAK) != 0) {
            printf("Erro: falha ao criar backup do arquivo original.\n");
            remove(ARQ_PRODUTOS_TMP);
            return 0;
        }
    }
    if (rename(ARQ_PRODUTOS_TMP, ARQ_PRODUTOS) != 0) {
        printf("Erro: não foi possível substituir o arquivo de produtos.\n");
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
    if (!f) {
        *qtd = 0;
        return 0;
    }
    *qtd = 0;
    while (1) {
        if (*qtd >= MAX_PRODUTOS) break;
        if (fread(&produtos[*qtd], sizeof(struct Produto), 1, f) != 1)
            break;
        (*qtd)++;
    }
    fclose(f);
    return 1;
}

void lerLinha(char *buf, int n) {
    if (fgets(buf, n, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = '\0';

    if (strlen(buf) == n-1 && buf[n-2] != '\n') {
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
    }
}

double coletarIngredientesText(char *descricao, int descSize, int *rendimento) {
    char buf[BUF_SIZE];
    char linha_desc[256];
    int n;
    double custo_total = 0.0;
    descricao[0] = '\0';
    do {
        printf("\n%sQuantos ingredientes tem essa receita? %s", YELLOW, RESET);
        lerLinha(buf, sizeof(buf));
        n = atoi(buf);
        if (n < 1 || n > MAX_INGR) {
            printf("%s[!] Por favor, digite um numero entre 1 e %d.%s\n", RED, MAX_INGR, RESET);
        }
    } while (n < 1 || n > MAX_INGR);
    for (int i = 0; i < n; i++) {
        char nome[128];
        int tipo;
        double preco, quantidade, custo;
        printf("\n%s%s> Ingrediente %d%s\n", BOLD, MAGENTA, i + 1, RESET);
        do {
            printf("%sNome: %s", CYAN, RESET);
            lerLinha(nome, sizeof(nome));
            if (strlen(nome) == 0) printf("%s[!] O nome nao pode ser vazio.%s\n", RED, RESET);
        } while (strlen(nome) == 0);
        do {
            printf("%sTipo (1=preco/kg | 2=preco por unidade): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            tipo = atoi(buf);
            if (tipo != 1 && tipo != 2) {
                printf("%s[!] Opcao invalida. Digite 1 ou 2.%s\n", RED, RESET);
            }
        } while (tipo != 1 && tipo != 2);
        if (tipo == 2) {
            do {
                printf("%sPreco por unidade (R$): %s", CYAN, RESET);
                lerLinha(buf, sizeof(buf));
                preco = atof(buf);
                if (preco <= 0) printf("%s[!] O preco deve ser maior que zero.%s\n", RED, RESET);
            } while (preco <= 0);
            do {
                printf("%sQuantidade usada (unidades): %s", CYAN, RESET);
                lerLinha(buf, sizeof(buf));
                quantidade = atof(buf);
                if (quantidade <= 0) printf("%s[!] A quantidade deve ser maior que zero.%s\n", RED, RESET);
            } while (quantidade <= 0);
            custo = preco * quantidade;
            snprintf(linha_desc, sizeof(linha_desc),
                     "  [*] %s: %.0f un x R$ %.2f = R$ %.2f\n",
                     nome, quantidade, preco, custo);
        } else {
            do {
                printf("%sPreco por KG (R$): %s", CYAN, RESET);
                lerLinha(buf, sizeof(buf));
                preco = atof(buf);
                if (preco <= 0) printf("%s[!] O preco deve ser maior que zero.%s\n", RED, RESET);
            } while (preco <= 0);
            do {
                printf("%sQuantidade usada (gramas): %s", CYAN, RESET);
                lerLinha(buf, sizeof(buf));
                quantidade = atof(buf);
                if (quantidade <= 0) printf("%s[!] A quantidade deve ser maior que zero.%s\n", RED, RESET);
            } while (quantidade <= 0);
            custo = (preco / 1000.0) * quantidade;
            snprintf(linha_desc, sizeof(linha_desc),
                     "  [*] %s: %.0fg x R$ %.2f/kg = R$ %.2f\n",
                     nome, quantidade, preco, custo);
        }
        custo_total += custo;
        if ((int)strlen(descricao) + (int)strlen(linha_desc) < descSize - 1)
            strncat(descricao, linha_desc, descSize - strlen(descricao) - 1);
        printf("%s[->] Custo de %s: %sR$ %.2f%s\n", GREEN, nome, BOLD, custo, RESET);
    }
    do {
        printf("\n%sRendimento da receita (quantas unidades produz): %s", YELLOW, RESET);
        lerLinha(buf, sizeof(buf));
        *rendimento = atoi(buf);
        if (*rendimento <= 0) {
             printf("%s[!] Rendimento deve ser pelo menos 1 unidade.%s\n", RED, RESET);
        }
    } while (*rendimento <= 0);

    return custo_total;
}

void calcularTudo(struct Produto *p) {
    double custo_base_unitario;
    if ((*p).modo == 1) {
        custo_base_unitario = (*p).preco_custo;
    } else {
        if ((*p).rendimento <= 0) (*p).rendimento = 1;
        double despesas_por_unidade = (*p).despesas_variaveis / (double)(*p).rendimento;
        custo_base_unitario = ((*p).investimento_total / (double)(*p).rendimento) + despesas_por_unidade;
    }
    double total_despesas_fixas = config.gasto_agua + config.gasto_luz + config.gasto_gas;
    double rateio_fixo_por_unidade = 0.0;
    if (config.producao_mensal_unidades > 0) {
        rateio_fixo_por_unidade = total_despesas_fixas / (double)config.producao_mensal_unidades;
    }
    (*p).custo_unitario = custo_base_unitario + rateio_fixo_por_unidade;
    if ((*p).usar_mei_comercio) {
        (*p).imposto_percent = 4.0;
    }
    (*p) = validarPercentuaisProduto(*p);
    double lucro_valor = (*p).custo_unitario * ((*p).lucro_produtor_percent / 100.0);
    double preco_com_lucro = (*p).custo_unitario + lucro_valor;
    double total_percent = (*p).imposto_percent + (*p).taxa_cartao_percent;
    if (total_percent >= MAX_PERCENT) total_percent = HALF_MAX_PERCENT * 2;
    (*p).preco_produtor = preco_com_lucro / (1.0 - (total_percent / 100.0));
}

double lerDoubleValidado(const char *msg, double valorAtual) {
    char buf[BUF_SIZE];
    double valor;
    int valido = 0;
    while (!valido) {
        printf("%s%s [ENTER para manter %.2f]: %s", CYAN, msg, valorAtual, RESET);
        lerLinha(buf, sizeof(buf));
        if (buf[0] == '\0') {
            return valorAtual;
        }
        char *endptr;
        valor = strtod(buf, &endptr);
        if (endptr != buf && *endptr == '\0') {
            valido = 1;
        } else {
            imprimir_aviso("Entrada invalida! Digite um numero valido.");
        }
    }
    return valor;
}

int lerIntValidado(const char *msg, int valorAtual) {
    char buf[BUF_SIZE];
    int valor;
    int valido = 0;
    while (!valido) {
        printf("%s%s [ENTER para manter %d]: %s", CYAN, msg, valorAtual, RESET);
        lerLinha(buf, sizeof(buf));
        if (buf[0] == '\0') {
            return valorAtual;
        }
        char *endptr;
        valor = (int)strtol(buf, &endptr, 10);
        if (endptr != buf && *endptr == '\0') {
            valido = 1;
        } else {
            imprimir_aviso("Entrada invalida! Digite um numero inteiro valido.");
        }
    }
    return valor;
}

void configurarDespesasFixas() {
    imprimir_cabecalho("VISUALIZAR DESPESAS FIXAS MENSAIS");
    printf("\n%s%sDESPESAS FIXAS ATUAIS:%s\n", BOLD, YELLOW, RESET);
    imprimir_valor("Agua", config.gasto_agua);
    imprimir_valor("Luz", config.gasto_luz);
    imprimir_valor("Gas", config.gasto_gas);
    printf("%sProducao mensal          :%s %d unidades\n", CYAN, RESET, config.producao_mensal_unidades);

    double total_fixo = config.gasto_agua + config.gasto_luz + config.gasto_gas;
    printf("\n");
    imprimir_valor("TOTAL DE DESPESAS FIXAS", total_fixo);

    if (config.producao_mensal_unidades > 0) {
        double rateio = total_fixo / (double)config.producao_mensal_unidades;
        printf("%sRateio por unidade       :%s R$ %.2f\n", CYAN, RESET, rateio);
    }

    char buf[BUF_SIZE];
    printf("\n%sDeseja alterar essas configuracoes? (s/n): %s", YELLOW, RESET);
    lerLinha(buf, sizeof(buf));

    if (buf[0] == 's' || buf[0] == 'S') {
        printf("\n");
        imprimir_secao("EDITAR DESPESAS FIXAS");
        config.gasto_agua = lerDoubleValidado("Gasto mensal com AGUA (R$)", config.gasto_agua);
        config.gasto_luz  = lerDoubleValidado("Gasto mensal com LUZ (R$)", config.gasto_luz);
        config.gasto_gas  = lerDoubleValidado("Gasto mensal com GAS (R$)", config.gasto_gas);
        config.producao_mensal_unidades = lerIntValidado("Producao mensal (unidades/mes)", config.producao_mensal_unidades);
        if (!salvarConfigAtomic()) {
            imprimir_aviso("Falha ao salvar configuracoes em disco.");
        } else {
            imprimir_sucesso("Configuracoes atualizadas com sucesso!");
        }
    } else {
        printf("\nNenhuma alteracao realizada.\n");
    }
    pausar();
}

void listarProdutos(struct Produto produtos[], int qtd) {
    imprimir_cabecalho("LISTA DE PRODUTOS");
    if (qtd == 0) {
        printf("\nNenhum produto cadastrado.\n");
        return;
    }
    printf("%-4s | %-30s | %-12s | %-12s\n", "ID", "NOME", "CUSTO UN.", "PRECO VENDA");
    imprimir_linha('-', 70);
    for(int i = 0; i < qtd; i++) {
        printf("%03d  | %-30.30s | R$ %9.2f | %sR$ %9.2f%s\n",
            i + 1,
            produtos[i].nome,
            produtos[i].custo_unitario,
            GREEN, produtos[i].preco_produtor, RESET
        );
    }
    printf("\nTotal: %d produtos.\n", qtd);
}

void excluirProdutoIndex(struct Produto produtos[], int *qtd, int idx) {
    for (int i = idx; i < *qtd - 1; i++) {
        produtos[i] = produtos[i+1];
    }
    (*qtd)--;
}

void excluirProduto(struct Produto produtos[], int *qtd) {
    imprimir_cabecalho("EXCLUIR PRODUTO");
    listarProdutos(produtos, *qtd);
    if (*qtd == 0) { pausar(); return; }
    int id = lerIntValidado("\nDigite o ID do produto para excluir (0 para cancelar)", 0);
    if (id <= 0 || id > *qtd) return;
    int idx = id - 1;
    char buf[10];
    printf("%sTem certeza que deseja excluir '%s'? (s/n): %s", RED, produtos[idx].nome, RESET);
    lerLinha(buf, sizeof(buf));
    if (buf[0] == 's' || buf[0] == 'S') {
        excluirProdutoIndex(produtos, qtd, idx);
        imprimir_sucesso("Produto excluido.");
        salvarProdutosAtomic(produtos, *qtd);
    } else {
        printf("Operacao cancelada.\n");
    }
    pausar();
}

void editarProduto(struct Produto produtos[], int qtd) {
    if (qtd == 0) {
        imprimir_aviso("Nenhum produto para editar.");
        return;
    }
    listarProdutos(produtos, qtd);
    int id = lerIntValidado("\nDigite o ID do produto para editar (0 cancela)", 0);
    if (id <= 0 || id > qtd) return;
    int idx = id - 1;
    struct Produto *p = &produtos[idx];
    limpar_tela();
    printf("%sEDITANDO: %s%s\n", BOLD, p->nome, RESET);
    imprimir_linha('-', 50);
    while(1) {
        printf("\n1. Nome (%s)\n", p->nome);
        printf("2. Lucro Desejado (%.1f%%)\n", p->lucro_produtor_percent);
        printf("3. Taxas/Impostos\n");
        printf("4. Recalcular e Sair\n");
        int op = lerIntValidado("Opcao", 4);
        if (op == 1) {
            char buf[MAX_NOME];
            printf("Novo nome: ");
            lerLinha(buf, sizeof(buf));
            if (strlen(buf) > 0) strcpy(p->nome, buf);
        }
        else if (op == 2) {
            p->lucro_produtor_percent = lerDoubleValidado("Novo lucro (%)", p->lucro_produtor_percent);
        }
        else if (op == 3) {
            p->imposto_percent = lerDoubleValidado("Novo Imposto (%)", p->imposto_percent);
            p->taxa_cartao_percent = lerDoubleValidado("Nova Taxa Cartao (%)", p->taxa_cartao_percent);
        }
        else if (op == 4) {
            break;
        }
    }
    calcularTudo(p);
    imprimir_sucesso("Produto atualizado e recalculado!");
    salvarProdutosAtomic(produtos, qtd);
}

void calculoRapido() {
    imprimir_cabecalho("CALCULO RAPIDO (SIMULADOR)");
    printf("Este modo nao salva o produto, apenas simula o preco.\n\n");
    struct Produto simul;
    memset(&simul, 0, sizeof(simul));
    simul.modo = 1;
    simul.rendimento = 1;
    simul.preco_custo = lerDoubleValidado("Custo direto do produto (R$)", 0.0);
    simul.lucro_produtor_percent = lerDoubleValidado("Lucro desejado (%)", 30.0);
    simul.imposto_percent = lerDoubleValidado("Imposto (%)", 0.0);
    simul.taxa_cartao_percent = lerDoubleValidado("Taxa Cartao (%)", 2.0);
    calcularTudo(&simul);
    printf("\n%sRESULTADO:%s\n", BOLD, RESET);
    imprimir_valor("Custo Final", simul.custo_unitario);
    imprimir_valor("PRECO SUGERIDO", simul.preco_produtor);
    pausar();
}

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
        int valido = 0;
        while (!valido) {
            printf("\n%sOpcao: %s", BOLD, RESET);
            lerLinha(buf, sizeof(buf));
            char *endptr;
            opc = (int)strtol(buf, &endptr, 10);
            if (endptr != buf && *endptr == '\0' && opc >= 1 && opc <= 5) {
                valido = 1;
            } else {
                imprimir_aviso("Entrada invalida! Digite um numero entre 1 e 5.");
            }
        }
        if (opc == 1) {
            if (salvarProdutosAtomic(produtos, *qtd))
                imprimir_sucesso("Produtos salvos com sucesso!");
            else
                imprimir_erro("Falha ao salvar produtos!");
            pausar();
        } else if (opc == 2) {
             editarProduto(produtos, *qtd);
             pausar();
             break;
        } else if (opc == 3) {
            if (idxRecente >= 0 && idxRecente < *qtd) {
                printf("%s%sTem certeza que deseja excluir o produto \"%s\"? (s/n): %s", BOLD, RED, produtos[idxRecente].nome, RESET);
                lerLinha(buf, sizeof(buf));
                if (buf[0] == 's' || buf[0] == 'S') {
                    excluirProdutoIndex(produtos, qtd, idxRecente);
                    if (!salvarProdutosAtomic(produtos, *qtd))
                        imprimir_aviso("Falha ao salvar após exclusão.");
                    imprimir_sucesso("Produto excluido com sucesso!");
                } else {
                    printf("Operacao cancelada.\n");
                }
                pausar();
            } else {
                imprimir_erro("Produto invalido para exclusao.");
                pausar();
            }
            break;
        } else if (opc == 4) {
            listarProdutos(produtos, *qtd);
            pausar();
        } else if (opc == 5) {
            break;
        }
    }
}

void menuGerenciarProdutos(struct Produto produtos[], int *qtd) {
    char buf[BUF_SIZE];
    int opc;
    do {
        imprimir_cabecalho("GERENCIAR PRODUTOS");
        printf("\n%s1%s - Cadastrar novo produto\n", GREEN, RESET);
        printf("%s2%s - Listar produtos\n", GREEN, RESET);
        printf("%s3%s - Editar produto\n", GREEN, RESET);
        printf("%s4%s - Excluir produto\n", GREEN, RESET);
        printf("%s5%s - Voltar ao menu principal\n", YELLOW, RESET);

        int valido = 0;
        while (!valido) {
            printf("\n%sOpcao: %s", BOLD, RESET);
            lerLinha(buf, sizeof(buf));
            char *endptr;
            opc = (int)strtol(buf, &endptr, 10);
            if (endptr != buf && *endptr == '\0' && opc >= 1 && opc <= 5)
                valido = 1;
            else
                imprimir_erro("Opcao invalida!");
        }

        switch (opc) {
            case 1: cadastrarProduto(produtos, qtd); break;
            case 2: listarProdutos(produtos, *qtd); pausar(); break;
            case 3: editarProduto(produtos, *qtd); pausar(); break;
            case 4: excluirProduto(produtos, qtd); break;
            case 5: break;
        }
    } while (opc != 5);
}

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
    do {
        printf("\n%sNome do produto: %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        if (strlen(buf) < 2) {
            printf("%s[!] O nome deve ter pelo menos 2 caracteres.%s\n", RED, RESET);
        }
    } while (strlen(buf) < 2);
    strncpy(p.nome, buf, sizeof(p.nome) - 1);
    p.nome[sizeof(p.nome) - 1] = '\0';
    do {
        printf("\n%sModo de Custo:%s\n", CYAN, RESET);
        printf("  1 - Custo Direto (ex: revenda, valor fixo)\n");
        printf("  2 - Receita (soma de ingredientes)\n");
        printf("%sOpcao: %s", YELLOW, RESET);
        lerLinha(buf, sizeof(buf));
        p.modo = atoi(buf);
        if (p.modo != 1 && p.modo != 2) {
            printf("%s[!] Opcao invalida. Digite 1 ou 2.%s\n", RED, RESET);
        }
    } while (p.modo != 1 && p.modo != 2);
    if (p.modo == 1) {
        do {
            printf("%sPreco de custo por unidade (R$): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            p.preco_custo = atof(buf);
            if (p.preco_custo <= 0) {
                printf("%s[!] O custo deve ser maior que zero.%s\n", RED, RESET);
            }
        } while (p.preco_custo <= 0);
        p.rendimento = 1;
    } else {
        imprimir_secao("INGREDIENTES DA RECEITA");
        double custoCalc = coletarIngredientesText(p.ingredientes_desc, sizeof(p.ingredientes_desc), &p.rendimento);
        if (custoCalc <= 0.0) {
            imprimir_erro("Erro: Custo calculado invalido ou cancelado.");
            pausar();
            return;
        }
        p.investimento_total = custoCalc;
        do {
            printf("\n%sDespesas variaveis extras (embalagem, entrega) [R$]: %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            p.despesas_variaveis = atof(buf);
            if (p.despesas_variaveis < 0) {
                printf("%s[!] O valor nao pode ser negativo.%s\n", RED, RESET);
            }
        } while (p.despesas_variaveis < 0);
    }
    char resp_mei;
    do {
        printf("\n%sUsar regime MEI COMERCIO (imposto fixo 4%%)? (s/n): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        resp_mei = buf[0];
        if (resp_mei != 's' && resp_mei != 'S' && resp_mei != 'n' && resp_mei != 'N') {
            printf("%s[!] Responda com 's' para sim ou 'n' para nao.%s\n", RED, RESET);
        }
    } while (resp_mei != 's' && resp_mei != 'S' && resp_mei != 'n' && resp_mei != 'N');

    if (resp_mei == 's' || resp_mei == 'S') {
        p.usar_mei_comercio = 1;
        p.imposto_percent = 4.0;
        printf("%s[OK] MEI Comercio aplicado (4%%)%s\n", GREEN, RESET);
    } else {
        p.usar_mei_comercio = 0;
        do {
            printf("%sPercentual de imposto (%%): %s", CYAN, RESET);
            lerLinha(buf, sizeof(buf));
            p.imposto_percent = atof(buf);
            if (p.imposto_percent < 0 || p.imposto_percent >= 100) {
                printf("%s[!] Digite um valor entre 0 e 99.%s\n", RED, RESET);
            }
        } while (p.imposto_percent < 0 || p.imposto_percent >= 100);
    }
    do {
        printf("%sTaxa da maquininha/cartao (%%): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.taxa_cartao_percent = atof(buf);
        if (p.taxa_cartao_percent < 0){
            printf("%s[!] Valor invalido.%s\n", RED, RESET);
        }
    } while (p.taxa_cartao_percent < 0);
    do {
        printf("%sPercentual de lucro desejado (%%): %s", CYAN, RESET);
        lerLinha(buf, sizeof(buf));
        p.lucro_produtor_percent = atof(buf);
        if (p.lucro_produtor_percent <= 0) printf("%s[!] O lucro deve ser maior que zero.%s\n", RED, RESET);
    } while (p.lucro_produtor_percent <= 0);

    p = validarPercentuaisProduto(p);
    calcularTudo(&p);

    produtos[*qtd] = p;
    int idxRecente = *qtd;
    (*qtd)++;
    if (!salvarProdutosAtomic(produtos, *qtd)) {
        imprimir_aviso("Falha ao salvar arquivo (produto ficou apenas na memoria RAM)");
    }
    imprimir_secao("RESULTADO DO CADASTRO");
    imprimir_sucesso("Produto cadastrado com sucesso!");
    printf("  [>] Custo Unitario: %sR$ %.2f%s\n", YELLOW, p.custo_unitario, RESET);
    printf("  [>] Preco Final:    %sR$ %.2f%s\n", GREEN, p.preco_produtor, RESET);
    menuPosCadastro(produtos, qtd, idxRecente);
}

int main() {
    struct Produto produtos[MAX_PRODUTOS];
    int qtd = 0;

    config.gasto_agua = 0.0;
    config.gasto_luz = 0.0;
    config.gasto_gas = 0.0;
    config.producao_mensal_unidades = 0;
    if (!carregarConfig()) {
        if (!salvarConfigAtomic()) {
            imprimir_aviso("Nao foi possivel criar config.dat com valores default.");
        }
    }
    carregarProdutos(produtos, &qtd);
    char buf[BUF_SIZE];
    int opc;
    do {
        limpar_tela();
        imprimir_cabecalho("SIPRI - SISTEMA DE PRECIFICACAO INTELIGENTE");
        imprimir_secao("MENU PRINCIPAL");
        printf("%s1%s - Gerenciar produtos\n", GREEN, RESET);
        printf("%s2%s - Calculo rapido\n", GREEN, RESET);
        printf("%s3%s - Visualizar despesas fixas\n", GREEN, RESET);
        printf("%s4%s - Sair\n", RED, RESET);
        int valido = 0;
        while (!valido) {
            printf("\n%sOpcao: %s", BOLD, RESET);
            lerLinha(buf, sizeof(buf));
            char *endptr;
            opc = (int)strtol(buf, &endptr, 10);
            if (endptr != buf && *endptr == '\0' && opc >= 1 && opc <= 4)
                valido = 1;
            else
                imprimir_erro("Opcao invalida!");
        }
        switch (opc) {
            case 1: menuGerenciarProdutos(produtos, &qtd); break;
            case 2: calculoRapido(); break;
            case 3: configurarDespesasFixas(); break;
            case 4:
                limpar_tela();
                printf("\n%s%sObrigado por usar o SIPRI!%s\n\n", BOLD, GREEN, RESET);
                break;
        }
    } while (opc != 4);
    return 0;
}
