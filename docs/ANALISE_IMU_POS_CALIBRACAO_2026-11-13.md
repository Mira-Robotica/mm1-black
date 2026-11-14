# Campos magnéticos após calibração — 13/11/2026

## Fontes e limites

- Gravação: `docker/data/20261114T000106.846246Z_bdf70a527099442ea4c11fe808df42d5/samples.csv`
  e `metadata.json`, iniciada em **13/11/2026, 21:01:06 (Brasília)**.
- Calibração precedente: `docker/data/cal-imu-1794614297924551824.jsonl`.
- 166 amostras em 83,041549 s; todos os componentes XYZ de Raw, Uncal e Cal
  presentes. As 166 células de azimute Raw estão vazias, por projeto.
- Inclinação GRV de −89,3067° a +87,0631°: o ensaio inclui frente para baixo/cima.
- Os arquivos originais foram preservados. Esta análise não altera o firmware,
  não aplica offset angular nem habilita azimute Raw.

A calibração terminou às 21:00:31, com `result=SAVED`, `dcd=SAVED`, `restore=OK`,
`mag_status=2`, `rv_status=3` e `accuracy_rad=0.0551757812`. O boot_id
`788934277a2b53cf` coincide com o da gravação seguinte. Portanto há confirmação
protocolar de SAVE e restauração, sem reboot entre as duas conexões. Esses
indicadores no término não certificam a precisão em todo o ensaio posterior.
O CSV de streaming não inclui status/precisão por amostra nem quaternions,
viés nativo, timestamps individuais ou sequência de cada relatório.

## Eixos Raw versus Uncalibrated e Calibrated

Usei medianas independentes por componente em seis janelas interiores aos
patamares, para reduzir o efeito de transições e observações discrepantes:
2–12, 18–27, 33–42, 48–57, 62–67 e 73–82 s. As últimas duas são posições
próximas da vertical; seu azimute não serve para avaliar estabilidade de direção.

| Raw | Associação Uncal | Associação Cal | Pearson entre medianas Raw/Uncal | Raw/Cal |
| --- | --- | --- | ---: | ---: |
| X | −X | −X | −0,997954 | −0,997954 |
| Y | −Y | −Y | −0,994708 | −0,994738 |
| Z | +Z | +Z | +0,985748 | +0,987604 |

As maiores correlações absolutas por linha estão na diagonal, sem evidência
favorável a uma permutação dos eixos. A associação anterior (−X,−Y,+Z) fica
mais bem sustentada, principalmente em Z. Trata-se da associação das variações,
**não** da igualdade numérica entre ADC e µT, nem de uma matriz física completa
validada. Soft iron pode misturar/escalar eixos, os relatórios são assíncronos e
há apenas seis orientações resumidas. Não se deve usar esses coeficientes como
uma conversão calibrada de Raw para µT.

Nas 166 linhas individuais, as correlações Raw/Uncal correspondentes são apenas
−0,78179, −0,94254 e +0,84720. Existe dispersão importante, inclusive nos patamares.
Um ajuste afim 3D de Raw para Uncal em todas as linhas deixa RMSE por eixo de
5,09 / 2,01 / 3,01 µT. Logo, uma transformação inferida dessas linhas não é uma
conversão de precisão. Nem a dispersão nem sua causa podem ser atribuídas
exclusivamente ao sensor: faltam os instantes individuais dos relatórios.

| Janela (s) | Raw mediano XYZ (ADC) | Uncal mediano XYZ (µT) | Cal mediano XYZ (µT) |
| --- | --- | --- | --- |
| 2–12 | −1444,5 / −508 / 105 | 71,719 / 30,031 / 6,094 | −4,531 / −1,656 / 10,625 |
| 18–27 | −1425 / −654 / 98 | 70,625 / 39,500 / 4,500 | −5,625 / 7,813 / 9,750 |
| 33–42 | −1573,5 / −624,5 / 98 | 77,406 / 37,313 / 4,906 | 1,156 / 5,625 / 10,156 |
| 48–57 | −1439 / −554 / 98 | 71,563 / 32,938 / 4,844 | −4,688 / 1,250 / 10,094 |
| 62–67 | −1743 / −461 / −19 | 87,688 / 24,625 / −4,719 | 11,438 / −7,063 / 0,531 |
| 73–82 | −1326 / −701 / −26 | 65,031 / 42,938 / −3,000 | −11,219 / 11,281 / 2,250 |

Mediana da diferença não é necessariamente diferença das medianas. Para estimar
o viés abaixo, a subtração foi feita linha a linha antes da mediana.

## Viés e consistência entre Uncal e Cal

A mediana de `Uncal − Cal` é **(+76,2500; +31,6875; −5,2500) µT**, com módulo
**82,7389 µT**. Em **163/166 linhas (98,2%)**, cada componente dessa diferença
está a no máximo **0,0625 µT** da mediana (um passo Q4). Isso sustenta fortemente
que os dois relatórios têm os mesmos eixos e que Cal remove esse viés.
As três exceções ocorrem em 4,813 s, 52,734 s e 61,847 s; a maior discrepância
vetorial é 13,678 µT. Sem sincronização/viés nativo registrado, não é possível
distinguir evolução do estimador, pareamento temporal e outros efeitos nessas linhas.

Na gravação anterior a diferença mediana era (+24,3125; +7,0625; −1,1250) µT.
O valor agora é muito diferente e a nova sessão contém um SAVE confirmado.
Não é um experimento controlado que permita atribuir toda mudança exclusivamente
à calibração: montagem, ambiente e história do estimador também podem contribuir.

Segundo o [SH-2 Reference Manual v1.9, §§6.5.15–6.5.17](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf),
Raw contém medidas dependentes do sensor, Uncal contém correção soft iron mas
mantém hard iron e reporta sua estimativa separadamente; Cal incorpora ambas as
correções. O monitor atualmente grava o vetor Uncal, sem subtrair a estimativa
separada. Calibrar o dispositivo não transforma o relatório Uncal em Cal.

## A calibração foi salva; a estabilidade magnética ainda não foi demonstrada

O módulo de Cal varia de **5,48 a 34,30 µT**, mediana **13,47 µT**; os percentis
10 e 90 são **10,15 e 24,24 µT**. A dispersão não está restrita às transições:
na janela 2–12 s, esses percentis são 10,09 e 25,00 µT. Um vetor de campo
constante, sujeito apenas a rotações, preservaria seu módulo. Os dados não
satisfazem bem esse modelo, mesmo após a remoção do viés.

Outra evidência é a mudança de azimute RV nos patamares quase imóveis do GRV:

| Janela (s) | Amplitude GRV | Variação RV entre primeiro/último ponto |
| --- | ---: | ---: |
| 2–12 | 0,062° | +27,22° |
| 18–27 | 0,045° | +36,19° |
| 33–42 | 0,106° | +26,10° |
| 48–57 | 0,106° | −26,83° |

Isso é compatível com correções de heading da fusão frente a um campo problemático
ou um estimador ainda em acomodação. Não identifica sozinho interferência local,
calibração residual, problema físico ou transporte. O norte absoluto não foi
validado por referência externa. A metadata registra 508 erros I/O acumulados
antes do streaming, mas não prova que eles originaram essa instabilidade.

## Consequência para azimute Raw/Uncal

A hipótese do operador é sustentada: calcular direção diretamente de um vetor
com viés grande dá a direção do **campo mais viés**, não necessariamente o norte.
Uncal X permanece positivo (51,4375–90,1875 µT) e Y também
(15,875–49,750 µT). Nos patamares horizontais, seu heading pouco acompanha os
giros. Nenhum ajuste angular constante remove um viés vetorial: é necessário
corrigir os componentes antes de calcular a direção.

Raw também exige orientação, escalas e calibração próprias; inverter X/Y é
insuficiente. O azimute Raw deve continuar indisponível enquanto essa cadeia
não estiver validada. Raw e Uncal permanecem úteis para diagnóstico de campo,
interferência e calibração, mas não devem ser tratados como bússolas independentes.

Para comparar uma direção magnética com RV, o candidato é o campo **Calibrated**
com compensação de inclinação. Alternativamente, Uncal menos o viés **nativo do
mesmo relatório** permite verificar a relação com Cal. A diferença mediana desta
gravacão não deve ser fixada no firmware como calibração universal. Mesmo Cal
não está validado como referência de azimute nesta sessão, pela instabilidade acima.

Nas posições próximas de ±90°, a projeção horizontal da frente +X se aproxima
de zero. O azimute da frente fica mal condicionado e, exatamente vertical,
é indefinido. Esses pontos ajudam a investigar eixos/campos, mas não servem
para certificar a precisão do azimute. Não confundir isso com erro de sinal.

Próximo ensaio útil: patamares imóveis em suporte não magnético, posição espacial
controlada, com registro dos quaternions, status/precisão, timestamps por relatório
e viés hard iron nativo. Isso permite separar variação do campo, evolução da
fusão e diferenças temporais entre os relatórios.
