# Rota RT64 retirada

Thiago determinou em 25/09/2026 eliminar o RT64 e toda emulação.
A integração, seus adaptadores RSP/RDP e seus runners foram retirados da
árvore ativa. Nenhuma parte dessa rota será usada pelo novo renderizador.

As provas anteriores e seus limites permanecem no histórico Git, no
commit `b1753bb01911fe0ffcdb18076967fa021c7d7873`. Os arquivos privados
continuam fora do repositório e não constituem dependências do novo caminho.

A arquitetura vigente está em [port/native/PLAN.md](../native/PLAN.md).
