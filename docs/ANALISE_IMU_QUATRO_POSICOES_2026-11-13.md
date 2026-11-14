# Análise da gravação de quatro posições da trena

Gravação `docker/data/20261113T221649.744766Z_ad67b111695e41a88ae7a866ddcc4171/`,
iniciada em 13/11/2026 às 19:16:49 no fuso America/Sao_Paulo. Foram analisadas
173 linhas de `samples.csv`, cobrindo 86,81 s. As 16 grandezas implementadas estão
preenchidas em todas as linhas. Azimute Raw está vazio nas 173 linhas, conforme o
método anunciado no `metadata.json`. Arquivos de medição e metadata foram
preservados. O relato do operador informa trena nivelada sobre mesa e posições
espaçadas de aproximadamente 90°. O campo de notas da gravação está vazio.

## RV e GRV

Ambos usam exatamente a função `lab::orientation(w,x,y,z,1,0,0)` em
`src/lab/stream_service.cpp`. A frente é +X, azimute `atan2(fx,fy)`, inclinação
do feixe `atan2(fz,hypot(fx,fy))` e roll pela mesma expressão de quaternion.

O GRV tem referência arbitrária. Nesta sessão, a diferença circular GRV − RV tem
mediana **46,343872°**, desvio padrão **0,008911°** e amplitude **0,033448°**.
Ela se mantém quase constante durante as rotações. O CSV não inclui qualidade
ou precisão angular do RV nem quaternions. Esses dados não permitem estabelecer
por que a fusão magnética mantém esse comportamento ou validar seu norte absoluto.

Foram escolhidas janelas interiores aos patamares para excluir movimentos e
acomodação de inclinação. Os números são medianas por janela, sem alinhamento
angular. O último patamar é uma quinta orientação próxima da inicial, compatível
com um retorno após quatro giros. A posição final difere da inicial cerca de 10°
segundo RV/GRV.

| Patamar | Janela (s) | Linhas | RV (°) | GRV (°) | Uncal gravado (°) | Uncal com sinal corrigido (°) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 2–12 | 20 | 40,85 | 87,21 | 324,20 | 35,80 |
| 2 | 23–32 | 18 | 308,72 | 355,05 | 328,37 | 31,63 |
| 3 | 42–48 | 11 | 221,50 | 267,85 | 338,02 | 21,98 |
| 4 | 60–68 | 16 | 135,46 | 181,79 | 320,61 | 39,39 |
| Retorno | 78–86 | 16 | 50,77 | 97,11 | 319,99 | 40,01 |

As diferenças circulares consecutivas do GRV são aproximadamente −92,16°, −87,20°,
−86,06° e −84,68°. Os saltos em 0°/360° são apenas a representação angular.

## Correspondência dos componentes magnéticos

A comparação empírica das variações nas 173 linhas indica a seguinte associação.
Os sinais descrevem a relação entre componentes, sem conversão de ADC para µT.

| Componente Raw | Uncalibrated associado | Calibrated associado | Pearson Raw × Uncal |
| --- | --- | --- | --- |
| X | −X | −X | −0,849752 |
| Y | −Y | −Y | −0,977606 |
| Z | +Z | +Z | +0,869435 |

Não há evidência de troca de X com Y nesse conjunto. As maiores correlações
absolutas de cada componente são as da diagonal acima. A associação é compatível
com inversões de X e Y, como uma rotação de 180° em torno de Z entre referenciais.
Ela constitui evidência empírica sobre estes dados. Uma transformação completa
entre os eixos físicos Raw e os da fusão também depende da orientação configurada,
das escalas entre eixos e da calibração soft iron. O teste quase horizontal não
isola rotações em X/Y, portanto não valida esse mapeamento em três dimensões.
O azimute Raw continua indisponível no firmware.

Uncalibrated e Calibrated correspondem diretamente em X, Y e Z, com correlações
por eixo de **0,999986**, **0,999986** e **0,999603**. A diferença
`Uncalibrated − Calibrated` tem mediana aproximadamente
**(+24,3125, +7,0625, −1,1250) µT**, com desvios padrão
**(0,0430, 0,0548, 0,1425) µT**. É compatível com a remoção do viés de hard iron.
Os relatórios são assíncronos e o CSV não grava o viés estimado pelo sensor,
portanto essa diferença é uma estimativa observada entre os dois relatórios.

O manual SH-2 especifica Raw em contagens dependentes do sensor, Uncalibrated com
correção soft iron e viés de hard iron separado, e Calibrated com ambas as
correções. Referência primária, §§6.5.15–6.5.17 e §4.3.4 sobre orientação:
[SH-2 Reference Manual v1.9](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf).

Medianas XYZ por patamar, com unidades explícitas:

| Patamar | Raw XYZ (ADC) | Uncal XYZ (µT) | Cal XYZ (µT) |
| --- | --- | --- | --- |
| 1 | −1648,5 / −754 / 159 | 77,906 / 55,188 / 9,000 | 53,562 / 48,156 / 10,156 |
| 2 | −1690 / −684 / 99 | 80,469 / 49,062 / 6,250 | 56,156 / 42,062 / 7,406 |
| 3 | −1678 / −473 / 132 | 80,375 / 32,125 / 8,500 | 56,062 / 25,062 / 9,625 |
| 4 | −1322 / −675 / 318 | 62,562 / 49,375 / 17,719 | 38,250 / 42,344 / 18,875 |
| Retorno | −1620 / −862 / 215,5 | 76,438 / 63,938 / 12,344 | 52,125 / 56,906 / 13,500 |

## Erro de sinal do azimute magnético

A implementação inicial seguia `atan2(-Nl,Nf)` do plano. Com
`l=cross(up,front)`, +Y é a esquerda. Quando a frente aponta para leste em ENU,
o norte está à esquerda, logo `Nf=0` e `Nl>0`. O azimute da frente deve ser +90°.
A fórmula antiga devolvia 270° nesse caso.

A expressão compatível com a convenção de produção é:

```text
azimuth_mag = wrap360(degrees(atan2(Nl,Nf)))
```

Com a trena perfeitamente nivelada, equivale a `atan2(My,Mx)`.
A compensação de inclinação por GRV permanece necessária e usa a mesma vertical.
O sinal foi corrigido em `src/lab/orientation.cpp`. Não foi aplicado offset
angular, declinação, subtração de viés ou alinhamento ao RV/GRV.

O teste automatizado agora constrói um campo mundial `(0,30,-20)` em ENU,
transforma-o para o corpo em 54 combinações de yaw/pitch/roll e compara o azimute
magnético recuperado ao azimute de `orientation()` da produção. Isso acrescenta
uma referência geométrica independente dos exemplos de quadrantes do plano.

O novo firmware anuncia `mag_azimuth_method=atan2_left_forward_v2`.
O cliente registra a expressão correspondente, aceitando também gravações e
firmwares anteriores, cujo método ausente significa `atan2_minus_left_forward_v1`.
A troca de sinal dá exatamente `wrap360(-azimute_antigo)` para um mesmo par de
vetor/GRV. A coluna corrigida da tabela foi calculada assim apenas para análise.
A reconstrução independente da vertical com inclinação/roll gravados reproduz
os valores com erro angular absoluto mediano de 0,000012° e máximo de 0,00365°.
O limite dessa reconstrução é a ausência dos quaternions e dos instantes de cada
relatório no CSV.

## O que continua estranho nos dados

A correção do sinal não faz o azimute magnético acompanhar os giros de 90°.
Durante toda a gravação, Uncal X ficou entre **49,375 e 84,000 µT** e Uncal Y
entre **29,5625 e 69,9375 µT**, ambos positivos. Cal X ficou entre
**25,0625 e 59,625 µT**, e Cal Y entre **22,5625 e 62,875 µT**, também positivos.
A nuvem horizontal permanece no primeiro quadrante dos dois relatórios.

Uma simples permutação/inversão de eixos não explica esse campo quase fixo no
corpo durante uma volta da trena. O comportamento é compatível com uma componente
magnética local dominante, viés residual ou perturbação do ambiente/montagem.
A origem específica não pode ser identificada apenas com esta gravação.
O rótulo Calibrated informa o processamento aplicado pelo sensor e não comprova
que a calibração atual eliminou a perturbação observada.

Para validar os eixos físicos completos é necessário um ensaio com rotações
controladas também em pitch e roll e uma referência física dos eixos da placa.
Para investigar a componente local, comparar a mesma sequência em suporte
não magnético e em outro local, com montagem e condições registradas, permitiria
separar melhor a contribuição do ambiente e a que acompanha a trena.
