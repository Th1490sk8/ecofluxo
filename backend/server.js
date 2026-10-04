// ==========================================================
// PROJETO ECOFLUXO - Servidor Backend
// Recebe dados do ESP32-S3, grava no MongoDB Atlas
// e disponibiliza uma API para o site (dashboard).
// ==========================================================

require('dotenv').config();
const express = require('express');
const mongoose = require('mongoose');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;
const MONGODB_URI = process.env.MONGODB_URI;

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public'))); // serve o dashboard

// ---------------- CONEXÃO COM MONGODB ATLAS ----------------
mongoose.connect(MONGODB_URI)
  .then(() => console.log('Conectado ao MongoDB Atlas com sucesso!'))
  .catch((erro) => console.error('Erro ao conectar no MongoDB:', erro));

// ---------------- MODELO (SCHEMA) ----------------
const leituraSchema = new mongoose.Schema({
  temperatura: { type: Number, required: true },
  umidade: { type: Number, required: true },
  gas: { type: Number, required: true },
  status: { type: String, enum: ['normal', 'atencao', 'critico'], required: true },
  exaustor: { type: Boolean, default: false }, // true = ventoinha 12V ligada
  emergencia: { type: Boolean, default: false }, // true = botão de emergência ativado (força o exaustor ligado)
  // Estimativas de ppm por tipo de gás, calculadas no ESP32 a partir da
  // mesma leitura do MQ-135 usando curvas de calibração genéricas do
  // datasheet (não são medições independentes — ver nota no firmware).
  co2: { type: Number },
  nh3: { type: Number },
  alcool: { type: Number },
  tolueno: { type: Number },
  aceton: { type: Number },
  criadoEm: { type: Date, default: Date.now }
});

const Leitura = mongoose.model('Leitura', leituraSchema);

// ---------------- ROTAS DA API ----------------

// Recebe uma nova leitura do ESP32-S3
app.post('/api/dados', async (req, res) => {
  try {
    const { temperatura, umidade, gas, status, exaustor, emergencia, co2, nh3, alcool, tolueno, aceton } = req.body;

    if (temperatura === undefined || umidade === undefined || gas === undefined || !status) {
      return res.status(400).json({ erro: 'Campos obrigatórios ausentes.' });
    }

    const novaLeitura = new Leitura({
      temperatura,
      umidade,
      gas,
      status,
      exaustor: Boolean(exaustor),
      emergencia: Boolean(emergencia),
      co2,
      nh3,
      alcool,
      tolueno,
      aceton
    });
    await novaLeitura.save();

    console.log(`Nova leitura salva: temp=${temperatura} umid=${umidade} gas=${gas} status=${status} exaustor=${novaLeitura.exaustor} co2=${co2} nh3=${nh3} alcool=${alcool} tolueno=${tolueno} aceton=${aceton}`);
    res.status(201).json({ mensagem: 'Leitura salva com sucesso.', dados: novaLeitura });
  } catch (erro) {
    console.error(erro);
    res.status(500).json({ erro: 'Erro ao salvar a leitura.' });
  }
});

// Retorna as últimas N leituras (padrão: 50) — usado pelos gráficos
app.get('/api/dados', async (req, res) => {
  try {
    const limite = parseInt(req.query.limite) || 50;
    const leituras = await Leitura.find()
      .sort({ criadoEm: -1 })
      .limit(limite);

    res.json(leituras.reverse()); // ordem cronológica para o gráfico
  } catch (erro) {
    res.status(500).json({ erro: 'Erro ao buscar leituras.' });
  }
});

// Retorna apenas a leitura mais recente — usado pelo card de status
app.get('/api/dados/ultimo', async (req, res) => {
  try {
    const ultima = await Leitura.findOne().sort({ criadoEm: -1 });
    res.json(ultima || {});
  } catch (erro) {
    res.status(500).json({ erro: 'Erro ao buscar última leitura.' });
  }
});

// Estatísticas simples (contagem por status) — opcional, útil para relatórios
app.get('/api/dados/estatisticas', async (req, res) => {
  try {
    const stats = await Leitura.aggregate([
      { $group: { _id: '$status', total: { $sum: 1 } } }
    ]);
    res.json(stats);
  } catch (erro) {
    res.status(500).json({ erro: 'Erro ao buscar estatísticas.' });
  }
});

app.listen(PORT, () => {
  console.log(`Servidor EcoFluxo rodando na porta ${PORT}`);
});
